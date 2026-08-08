#pragma once

#include <cstddef>
#include <filesystem>
#include <iosfwd>
#include <vector>

namespace xormap_rgb565 {

struct Paths {
    std::filesystem::path source_images;
    std::filesystem::path source_manifest;
    std::filesystem::path converted_images;
    std::filesystem::path converted_manifest;
    std::filesystem::path results;
};

struct Options {
    Paths paths;
    std::vector<std::size_t> k_values;
    std::size_t workers = 0;
    std::size_t correlation_samples = 3000;
    std::size_t scatter_samples = 3000;
    bool all_images = false;
    bool overwrite = false;
};

[[nodiscard]] std::vector<std::size_t> k_values(std::size_t first,
                                                std::size_t step,
                                                std::size_t last);
void verify(std::ostream& progress);
void convert_all(const Options& options, std::ostream& progress);
void run_all(const Options& options, std::ostream& progress);
void sweep_legacy(const Options& options, std::ostream& progress);
void sweep_all(const Options& options, std::ostream& progress);

}  // namespace xormap_rgb565
