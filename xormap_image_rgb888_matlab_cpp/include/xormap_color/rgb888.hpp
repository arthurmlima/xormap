#pragma once

#include <cstddef>
#include <filesystem>
#include <iosfwd>
#include <vector>

namespace xormap_color::rgb888 {

struct Paths {
    std::filesystem::path images;
    std::filesystem::path manifest;
    std::filesystem::path results;
};

struct Options {
    Paths paths;
    std::vector<std::size_t> k_values;
    std::size_t workers = 0;
    std::size_t correlation_samples = 3000;
};

[[nodiscard]] std::vector<std::size_t> k_values(std::size_t first,
                                                std::size_t step,
                                                std::size_t last);
void verify(std::ostream& progress);
void run_tests(const Options& options, std::ostream& progress);

}  // namespace xormap_color::rgb888
