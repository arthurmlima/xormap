// Independent cross-check of test_vectors.mem (produced by
// gen_test_vectors.m / xormap_matlab's xormap_transform) against
// xormap_cpp's xormap::transform, at K=512.
//
// For every seed block in test_vectors.mem, this re-derives each
// iteration from the seed using xormap::transform and diffs the result
// against the value MATLAB already wrote for that line. It never reads
// the "expected" lines as input to the computation, only as the
// comparison target, so a match here means MATLAB and C++ agree
// independently.

#include "xormap.hpp"
#include "test_params.hpp"

#include <cstddef>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string to_hex(const xormap::Integer& value, std::size_t k)
{
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(static_cast<int>(k / 4)) << value;
    return oss.str();
}

}  // namespace

int main()
{
    std::ifstream in("../test_vectors.mem");
    if (!in) {
        std::cerr << "Could not open test_vectors.mem\n";
        return 1;
    }

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty()) {
            lines.push_back(line);
        }
    }

    const std::size_t lines_per_seed = kNumSteps + 1;
    if (lines.size() != kNumSeeds * lines_per_seed) {
        std::cerr << "Expected " << kNumSeeds * lines_per_seed << " lines, got "
                   << lines.size() << "\n";
        return 1;
    }

    std::size_t mismatches = 0;
    std::size_t checked = 0;

    for (std::size_t s = 0; s < kNumSeeds; ++s) {
        const std::size_t base = s * lines_per_seed;
        xormap::Integer value(std::string("0x") + lines[base]);

        for (std::size_t step = 1; step <= kNumSteps; ++step) {
            value = xormap::transform(value, kK);
            const std::string actual = to_hex(value, kK);
            const std::string& expected = lines[base + step];
            ++checked;
            if (actual != expected) {
                ++mismatches;
                std::cerr << "MISMATCH seed=" << s << " step=" << step << "\n"
                          << "  matlab : " << expected << "\n"
                          << "  cpp    : " << actual << "\n";
            }
        }
    }

    std::cout << "Checked " << checked << " iterations across " << kNumSeeds
              << " seeds at K=" << kK << ".\n";

    if (mismatches != 0) {
        std::cout << mismatches << " mismatch(es) between MATLAB and C++.\n";
        return 1;
    }

    std::cout << "C++ (xormap_cpp) matches MATLAB (xormap_matlab) for every iteration.\n";
    return 0;
}
