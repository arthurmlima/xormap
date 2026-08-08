#include "xormap_rgb565/evaluation.hpp"

#include <charconv>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

[[nodiscard]] std::size_t parse_size(std::string_view text,
                                     std::string_view option,
                                     bool allow_zero = false)
{
    std::size_t value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || parsed.ec != std::errc{} ||
        parsed.ptr != text.data() + text.size() || (!allow_zero && value == 0U)) {
        throw std::invalid_argument(std::string(option) +
                                    " requires a valid integer value");
    }
    return value;
}

[[nodiscard]] std::filesystem::path project_root(const char* executable)
{
    std::error_code error;
    const auto absolute = std::filesystem::weakly_canonical(
        std::filesystem::absolute(executable), error);
    if (!error) {
        const auto candidate = absolute.parent_path().parent_path();
        if (std::filesystem::exists(candidate / "CMakeLists.txt")) {
            return candidate;
        }
    }
    return std::filesystem::current_path();
}

void help()
{
    std::cout <<
        "Usage: xormap_rgb565 <command> [options]\n\n"
        "Commands:\n"
        "  verify     Run translated RGB565 MATLAB correctness tests\n"
        "  convert    Convert all 51 RGB888 TIFFs to packed RGB565 files\n"
        "  run-all    Generate K=512 reports (three MATLAB images; --all for 51)\n"
        "  sweep      Preserve the original three-image K=8:4:512 sweep\n"
        "  sweep-all  Calculate metrics for all 51 converted images in parallel\n"
        "  help       Show this help\n\n"
        "Path options:\n"
        "  --source-images PATH       RGB888 TIFF directory\n"
        "  --source-manifest PATH     RGB888 manifest CSV\n"
        "  --converted-images PATH    packed RGB565 output/input directory\n"
        "  --converted-manifest PATH  RGB565 manifest CSV\n"
        "  --results PATH             report directory\n\n"
        "Other options:\n"
        "  --threads N    native C++ workers (0 = hardware)\n"
        "  --overwrite    rewrite converted files\n"
        "  --all          run-all reports for all 51 images\n"
        "  --k-first N    K range first value\n"
        "  --k-step N     K range increment\n"
        "  --k-last N     K range last value\n"
        "  --samples N    correlation samples\n"
        "  --scatter N    report scatter samples (run-all)\n";
}

}  // namespace

int main(int argc, char** argv)
{
    try {
        const std::string command = argc > 1 ? argv[1] : "help";
        if (command == "help" || command == "--help" || command == "-h") {
            help();
            return 0;
        }
        if (command == "verify") {
            if (argc != 2) {
                throw std::invalid_argument("verify accepts no options");
            }
            xormap_rgb565::verify(std::cout);
            return 0;
        }
        if (command != "convert" && command != "run-all" &&
            command != "sweep" && command != "sweep-all") {
            throw std::invalid_argument("unknown command '" + command + "'");
        }

        const auto root = project_root(argv[0]);
        const auto rgb888_cpp = root.parent_path() / "xormap_image_rgb888_matlab_cpp";
        xormap_rgb565::Options options;
        options.paths.source_images = rgb888_cpp / "images";
        options.paths.source_manifest = options.paths.source_images / "manifest.csv";
        options.paths.converted_images = root / "images_rgb565";
        options.paths.converted_manifest = options.paths.converted_images / "manifest.csv";
        options.paths.results = root / "results";
        std::size_t first = command == "sweep" ? 8U : 24U;
        std::size_t step = command == "sweep" ? 4U : 24U;
        std::size_t last = command == "sweep" ? 512U : 384U;

        for (int i = 2; i < argc; ++i) {
            const std::string name = argv[i];
            if (name == "--overwrite" && command == "convert") {
                options.overwrite = true;
                continue;
            }
            if (name == "--all" && command == "run-all") {
                options.all_images = true;
                continue;
            }
            if (i + 1 >= argc) {
                throw std::invalid_argument(name + " requires a value");
            }
            const std::string value = argv[++i];
            if (name == "--source-images" && command == "convert") {
                options.paths.source_images = value;
            } else if (name == "--source-manifest" && command == "convert") {
                options.paths.source_manifest = value;
            } else if (name == "--converted-images") {
                options.paths.converted_images = value;
            } else if (name == "--converted-manifest") {
                options.paths.converted_manifest = value;
            } else if (name == "--results" && command != "convert") {
                options.paths.results = value;
            } else if (name == "--threads" &&
                       (command == "convert" || command == "sweep-all")) {
                options.workers = parse_size(value, name, true);
            } else if (name == "--k-first" &&
                       (command == "sweep" || command == "sweep-all")) {
                first = parse_size(value, name);
            } else if (name == "--k-step" &&
                       (command == "sweep" || command == "sweep-all")) {
                step = parse_size(value, name);
            } else if (name == "--k-last" &&
                       (command == "sweep" || command == "sweep-all")) {
                last = parse_size(value, name);
            } else if (name == "--samples" &&
                       (command == "sweep" || command == "sweep-all")) {
                options.correlation_samples = parse_size(value, name);
            } else if (name == "--scatter" && command == "run-all") {
                options.scatter_samples = parse_size(value, name);
            } else {
                throw std::invalid_argument(command + " does not accept " + name);
            }
        }

        if (command == "convert") {
            xormap_rgb565::convert_all(options, std::cout);
        } else if (command == "run-all") {
            xormap_rgb565::run_all(options, std::cout);
        } else {
            options.k_values = xormap_rgb565::k_values(first, step, last);
            if (command == "sweep") {
                xormap_rgb565::sweep_legacy(options, std::cout);
            } else {
                xormap_rgb565::sweep_all(options, std::cout);
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 2;
    }
}
