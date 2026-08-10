#include "xormap_color/rgb888.hpp"

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
        "Usage: xormap_rgb888 <command> [options]\n\n"
        "Commands:\n"
        "  verify     Run the translated MATLAB RGB888 correctness tests\n"
        "  run-tests  Run every image x K sweep task plus key sensitivity,\n"
        "             histogram, and PSNR analysis in one pass. Writes one\n"
        "             combined sweep_k_rgb888.csv and one sweep_k_rgb888.pdf.\n"
        "  help       Show this help\n\n"
        "run-tests options:\n"
        "  --images PATH    RGB888 TIFF directory\n"
        "  --manifest PATH  filename,volume,name manifest CSV\n"
        "  --results PATH   output directory\n"
        "  --threads N      native C++ worker count (0 = hardware, default)\n"
        "  --k-first N      first K (default 24)\n"
        "  --k-step N       K increment (default 24)\n"
        "  --k-last N       last K (default 384)\n"
        "  --samples N      horizontal-correlation samples (sweep pass, default 3000)\n";
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
            xormap_color::rgb888::verify(std::cout);
            std::cout << "All translated RGB888 MATLAB tests passed.\n";
            return 0;
        }
        if (command != "run-tests") {
            throw std::invalid_argument("unknown command '" + command + "'");
        }

        const auto root = project_root(argv[0]);
        xormap_color::rgb888::Options options;
        options.paths.images = root / "images";
        options.paths.manifest = options.paths.images / "manifest.csv";
        options.paths.results = root / "results";
        std::size_t first = 24U;
        std::size_t step = 24U;
        std::size_t last = 384U;

        for (int i = 2; i < argc; ++i) {
            const std::string name = argv[i];
            if (i + 1 >= argc) {
                throw std::invalid_argument(name + " requires a value");
            }
            const std::string value = argv[++i];
            if (name == "--images") {
                options.paths.images = value;
            } else if (name == "--manifest") {
                options.paths.manifest = value;
            } else if (name == "--results") {
                options.paths.results = value;
            } else if (name == "--threads") {
                options.workers = parse_size(value, name, true);
            } else if (name == "--k-first") {
                first = parse_size(value, name);
            } else if (name == "--k-step") {
                step = parse_size(value, name);
            } else if (name == "--k-last") {
                last = parse_size(value, name);
            } else if (name == "--samples") {
                options.correlation_samples = parse_size(value, name);
            } else {
                throw std::invalid_argument(command + " does not accept " + name);
            }
        }
        options.k_values = xormap_color::rgb888::k_values(first, step, last);
        xormap_color::rgb888::run_tests(options, std::cout);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 2;
    }
}
