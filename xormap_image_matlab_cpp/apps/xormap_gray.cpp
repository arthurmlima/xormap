#include "xormap_image/evaluation.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;

struct ParsedOptions {
    fs::path project_root;
    fs::path images;
    fs::path results;
    fs::path manifest;
    fs::path gray_csv;
    fs::path rgb_csv;
    fs::path output_csv;
    std::size_t threads = 0;
    std::size_t k_first = 0;
    std::size_t k_step = 0;
    std::size_t k_last = 0;
    std::size_t samples = 0;
    bool all = false;
    bool overwrite = false;
    bool help_requested = false;
    std::set<std::string> specified;
    std::vector<std::string> positional;
};

[[noreturn]] void usage_error(const std::string& message)
{
    throw std::invalid_argument(message + " (run 'xormap_gray help' for usage)");
}

std::size_t parse_size(const std::string& text, const char* option, bool allow_zero = true)
{
    if (text.empty() || text.front() == '-') {
        usage_error(std::string(option) + " expects a non-negative integer");
    }
    std::size_t consumed = 0;
    unsigned long long raw = 0;
    try {
        raw = std::stoull(text, &consumed, 10);
    } catch (const std::exception&) {
        usage_error(std::string(option) + " expects an integer, got '" + text + "'");
    }
    if (consumed != text.size() || raw > std::numeric_limits<std::size_t>::max()) {
        usage_error(std::string(option) + " expects an in-range integer, got '" + text + "'");
    }
    const auto value = static_cast<std::size_t>(raw);
    if (!allow_zero && value == 0) {
        usage_error(std::string(option) + " must be greater than zero");
    }
    return value;
}

bool is_project_root(const fs::path& path)
{
    return fs::is_regular_file(path / "CMakeLists.txt") &&
           fs::is_directory(path / "include" / "xormap_image") &&
           fs::is_regular_file(path / "README.md");
}

fs::path locate_project_root(const char* executable)
{
    if (const char* configured = std::getenv("XORMAP_IMAGE_CPP_ROOT")) {
        const fs::path path(configured);
        if (!is_project_root(path)) {
            usage_error("XORMAP_IMAGE_CPP_ROOT is not an xormap_image_matlab_cpp project");
        }
        return fs::absolute(path).lexically_normal();
    }

    std::error_code error;
    const fs::path working = fs::current_path(error);
    if (!error) {
        if (is_project_root(working)) {
            return working;
        }
        if (is_project_root(working / "xormap_image_matlab_cpp")) {
            return working / "xormap_image_matlab_cpp";
        }
        if (working.filename().string().rfind("build", 0) == 0 &&
            is_project_root(working.parent_path())) {
            return working.parent_path();
        }
    }

    fs::path executable_path(executable == nullptr ? "" : executable);
    executable_path = fs::weakly_canonical(fs::absolute(executable_path), error);
    if (!error) {
        const fs::path executable_directory = executable_path.parent_path();
        if (is_project_root(executable_directory)) {
            return executable_directory;
        }
        if (executable_directory.filename().string().rfind("build", 0) == 0 &&
            is_project_root(executable_directory.parent_path())) {
            return executable_directory.parent_path();
        }
    }

    // An installed executable has no source-tree dependency. Its defaults are
    // deliberately relative to the invocation directory and can be overridden
    // with normal CLI options or XORMAP_IMAGE_CPP_ROOT.
    return error ? fs::path(".") : working;
}

ParsedOptions parse_options(int argc, char** argv, int first_argument,
                            const fs::path& project_root)
{
    ParsedOptions options;
    options.project_root = project_root;
    options.images = project_root / "images";
    options.results = project_root / "results";

    for (int index = first_argument; index < argc; ++index) {
        const std::string argument = argv[index];
        const auto require_value = [&](const char* option) -> std::string {
            if (index + 1 >= argc) {
                usage_error(std::string(option) + " requires a value");
            }
            return argv[++index];
        };

        if (argument == "--images") {
            options.specified.insert(argument);
            options.images = require_value("--images");
        } else if (argument == "--results") {
            options.specified.insert(argument);
            options.results = require_value("--results");
        } else if (argument == "--manifest") {
            options.specified.insert(argument);
            options.manifest = require_value("--manifest");
        } else if (argument == "--gray") {
            options.specified.insert(argument);
            options.gray_csv = require_value("--gray");
        } else if (argument == "--rgb") {
            options.specified.insert(argument);
            options.rgb_csv = require_value("--rgb");
        } else if (argument == "--output") {
            options.specified.insert(argument);
            options.output_csv = require_value("--output");
        } else if (argument == "--threads") {
            options.specified.insert(argument);
            options.threads = parse_size(require_value("--threads"), "--threads");
        } else if (argument == "--k-first") {
            options.specified.insert(argument);
            options.k_first = parse_size(require_value("--k-first"), "--k-first", false);
        } else if (argument == "--k-step") {
            options.specified.insert(argument);
            options.k_step = parse_size(require_value("--k-step"), "--k-step", false);
        } else if (argument == "--k-last") {
            options.specified.insert(argument);
            options.k_last = parse_size(require_value("--k-last"), "--k-last", false);
        } else if (argument == "--samples") {
            options.specified.insert(argument);
            options.samples = parse_size(require_value("--samples"), "--samples", false);
        } else if (argument == "--all") {
            options.specified.insert(argument);
            options.all = true;
        } else if (argument == "--overwrite") {
            options.specified.insert(argument);
            options.overwrite = true;
        } else if (argument == "--help" || argument == "-h") {
            options.help_requested = true;
        } else if (argument.rfind("--", 0) == 0) {
            usage_error("unknown option '" + argument + "'");
        } else {
            options.positional.push_back(argument);
        }
    }
    if (options.manifest.empty()) {
        options.manifest = options.images / "manifest_gray.csv";
    }
    return options;
}

void print_help(std::ostream& output)
{
    output <<
        "xormap_gray - native C++ port of xormap_image_matlab\n\n"
        "Usage:\n"
        "  xormap_gray verify\n"
        "  xormap_gray run-all [common options] [--k-first K]\n"
        "  xormap_gray sweep [common options] [--k-first 8 --k-step 4 --k-last 512]\n"
        "  xormap_gray sweep-all [common options] [--k-first 24 --k-step 24 --k-last 384]\n"
        "  xormap_gray analysis [common options] [--k-first 24 --k-step 24 --k-last 384]\n"
        "  xormap_gray download [--all] [--overwrite] [--images DIR] [--threads N]\n"
        "  xormap_gray read-sweep <gray|rgb888|CSV> [--results DIR]\n"
        "  xormap_gray compare [--gray CSV] [--rgb CSV] [--output CSV]\n\n"
        "Common options:\n"
        "  --images DIR     SIPI TIFF directory (defaults to this project's images/)\n"
        "  --results DIR    output directory (defaults to this project's results/)\n"
        "  --manifest CSV   all-grayscale manifest (defaults to DIR/manifest_gray.csv)\n"
        "  --threads N      native C++ workers; 0 means hardware concurrency\n"
        "  --samples N      override adjacent-pixel samples (run-all defaults to\n"
        "                   5000 metrics / 3000 scatter; sweeps default to 3000)\n\n"
        "The sweep and analysis commands use the optimized transform only after\n"
        "'verify' proves it identical to the canonical MATLAB translation.\n";
}

void reject_positionals(const ParsedOptions& options, const char* command)
{
    if (!options.positional.empty()) {
        usage_error(std::string(command) + " does not accept positional arguments");
    }
}

void validate_options(const ParsedOptions& options,
                      const char* command,
                      std::initializer_list<std::string_view> allowed)
{
    const std::set<std::string_view> accepted(allowed.begin(), allowed.end());
    for (const std::string& option : options.specified) {
        if (accepted.count(option) == 0U) {
            usage_error(std::string(command) + " does not accept " + option);
        }
    }
}

std::vector<std::size_t> selected_grid(const ParsedOptions& options,
                                       std::size_t default_first,
                                       std::size_t default_step,
                                       std::size_t default_last)
{
    const std::size_t first = options.k_first == 0 ? default_first : options.k_first;
    const std::size_t step = options.k_step == 0 ? default_step : options.k_step;
    const std::size_t last = options.k_last == 0 ? default_last : options.k_last;
    return xormap_image::make_k_values(first, step, last);
}

fs::path sweep_alias_path(const std::string& source, const ParsedOptions& options)
{
    std::string alias(source.size(), '\0');
    std::transform(source.begin(), source.end(), alias.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    if (alias == "gray") {
        return options.results / "sweep_k_gray_all.csv";
    }
    if (alias == "rgb888") {
        return options.project_root.parent_path() / "xormap_image_rgb888_matlab_cpp" / "results" /
               "sweep_k_rgb888.csv";
    }
    return source;
}

std::string sweep_display_name(const std::string& source)
{
    std::string alias(source.size(), '\0');
    std::transform(source.begin(), source.end(), alias.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    if (alias == "gray") {
        return "grayscale (all SIPI grayscale images)";
    }
    if (alias == "rgb888") {
        return "RGB888 (all SIPI colour images)";
    }
    const std::string stem = fs::path(source).stem().string();
    return stem.empty() ? source : stem;
}

}  // namespace

int main(int argc, char** argv)
{
    try {
        if (argc < 2 || std::string(argv[1]) == "help" || std::string(argv[1]) == "--help" ||
            std::string(argv[1]) == "-h") {
            print_help(std::cout);
            return argc < 2 ? EXIT_FAILURE : EXIT_SUCCESS;
        }

        const std::string command = argv[1];
        const fs::path project_root = locate_project_root(argv[0]);
        const ParsedOptions options = parse_options(argc, argv, 2, project_root);
        if (options.help_requested) {
            print_help(std::cout);
            return EXIT_SUCCESS;
        }

        if (command == "verify") {
            validate_options(options, "verify", {});
            reject_positionals(options, "verify");
            xormap_image::verify_matlab_fast_path(std::cout);
        } else if (command == "run-all") {
            validate_options(options, "run-all",
                             {"--images", "--results", "--threads", "--k-first",
                              "--samples"});
            reject_positionals(options, "run-all");
            xormap_image::RunAllOptions run;
            run.paths = {options.images, options.results};
            run.k = options.k_first == 0 ? 512 : options.k_first;
            run.correlation_samples = options.samples == 0 ? 5000 : options.samples;
            run.scatter_samples = options.samples == 0 ? 3000 : options.samples;
            run.workers = options.threads;
            xormap_image::run_all(run, std::cout);
        } else if (command == "sweep") {
            validate_options(options, "sweep",
                             {"--images", "--results", "--threads", "--k-first",
                              "--k-step", "--k-last", "--samples"});
            reject_positionals(options, "sweep");
            xormap_image::SweepOptions sweep;
            sweep.paths = {options.images, options.results};
            sweep.k_values = selected_grid(options, 8, 4, 512);
            sweep.correlation_samples = options.samples == 0 ? 3000 : options.samples;
            sweep.workers = options.threads;
            xormap_image::sweep_three_images(sweep, std::cout);
        } else if (command == "sweep-all") {
            validate_options(options, "sweep-all",
                             {"--images", "--results", "--manifest", "--threads",
                              "--k-first", "--k-step", "--k-last", "--samples"});
            reject_positionals(options, "sweep-all");
            xormap_image::SweepOptions sweep;
            sweep.paths = {options.images, options.results};
            sweep.manifest_path = options.manifest;
            sweep.k_values = selected_grid(options, 24, 24, 384);
            sweep.correlation_samples = options.samples == 0 ? 3000 : options.samples;
            sweep.workers = options.threads;
            xormap_image::sweep_all_images(sweep, std::cout);
        } else if (command == "analysis") {
            validate_options(options, "analysis",
                             {"--images", "--results", "--manifest", "--threads",
                              "--k-first", "--k-step", "--k-last"});
            reject_positionals(options, "analysis");
            xormap_image::AnalysisOptions analysis;
            analysis.paths = {options.images, options.results};
            analysis.manifest_path = options.manifest;
            analysis.k_values = selected_grid(options, 24, 24, 384);
            analysis.workers = options.threads;
            xormap_image::analyze_all_images(analysis, std::cout);
        } else if (command == "download") {
            validate_options(options, "download",
                             {"--images", "--threads", "--all", "--overwrite"});
            reject_positionals(options, "download");
            xormap_image::DownloadOptions download;
            download.images_directory = options.images;
            download.all_images = options.all;
            download.overwrite = options.overwrite;
            download.workers = options.threads;
            xormap_image::download_images(download, std::cout);
        } else if (command == "read-sweep") {
            validate_options(options, "read-sweep", {"--results"});
            if (options.positional.size() != 1) {
                usage_error("read-sweep expects exactly one alias or CSV path");
            }
            const std::string source = options.positional.front();
            auto sweep = xormap_image::read_sweep_csv(
                sweep_alias_path(source, options), sweep_display_name(source));
            xormap_image::print_sweep_summary(sweep, std::cout);
        } else if (command == "compare") {
            validate_options(options, "compare",
                             {"--results", "--gray", "--rgb", "--output"});
            fs::path gray = options.results / "sweep_k_gray_all.csv";
            fs::path rgb = options.project_root.parent_path() /
                           "xormap_image_rgb888_matlab_cpp" / "results" /
                           "sweep_k_rgb888.csv";
            fs::path output = options.results / "comparison" /
                              "compare_rgb_gray_summary.csv";
            if (!options.gray_csv.empty()) {
                gray = options.gray_csv;
            }
            if (!options.rgb_csv.empty()) {
                rgb = options.rgb_csv;
            }
            if (!options.output_csv.empty()) {
                output = options.output_csv;
            }
            // compare accepts compact positional overrides: gray, rgb, output.
            if (options.positional.size() > 3) {
                usage_error("compare accepts at most three paths: gray rgb output");
            }
            if (!options.positional.empty() &&
                (!options.gray_csv.empty() || !options.rgb_csv.empty() ||
                 !options.output_csv.empty())) {
                usage_error("compare path options cannot be mixed with positional paths");
            }
            if (!options.positional.empty()) {
                gray = options.positional[0];
            }
            if (options.positional.size() >= 2) {
                rgb = options.positional[1];
            }
            if (options.positional.size() >= 3) {
                output = options.positional[2];
            }
            const auto gray_sweep = xormap_image::read_sweep_csv(gray, "grayscale");
            const auto rgb_sweep = xormap_image::read_sweep_csv(rgb, "RGB888");
            xormap_image::compare_sweeps(rgb_sweep, gray_sweep, output);
            std::cout << "Wrote " << output << '\n';
        } else {
            usage_error("unknown command '" + command + "'");
        }
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
