#ifndef XORMAP_IMAGE_EVALUATION_HPP
#define XORMAP_IMAGE_EVALUATION_HPP

#include <cstddef>
#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

namespace xormap_image {

struct EvaluationPaths {
    std::filesystem::path images_directory;
    std::filesystem::path results_directory;
};

struct RunAllOptions {
    EvaluationPaths paths;
    std::size_t k = 512;
    std::size_t correlation_samples = 5000;
    std::size_t scatter_samples = 3000;
    std::size_t workers = 0;
};

struct SweepOptions {
    EvaluationPaths paths;
    std::filesystem::path manifest_path;
    std::vector<std::size_t> k_values;
    std::size_t correlation_samples = 3000;
    std::size_t workers = 0;
};

struct AnalysisOptions {
    EvaluationPaths paths;
    std::filesystem::path manifest_path;
    std::vector<std::size_t> k_values;
    std::size_t workers = 0;
};

struct DownloadOptions {
    std::filesystem::path images_directory;
    bool all_images = false;
    bool overwrite = false;
    std::size_t workers = 0;
};

std::vector<std::size_t> make_k_values(std::size_t first,
                                       std::size_t step,
                                       std::size_t last);

void run_all(const RunAllOptions& options, std::ostream& progress);
void sweep_three_images(const SweepOptions& options, std::ostream& progress);
void sweep_all_images(const SweepOptions& options, std::ostream& progress);
void analyze_all_images(const AnalysisOptions& options, std::ostream& progress);
void download_images(const DownloadOptions& options, std::ostream& progress);

struct NormalizedSweepRow {
    std::string image;
    std::string volume;
    std::size_t k = 0;
    std::size_t height = 0;
    std::size_t width = 0;
    std::size_t num_pixels = 0;
    double entropy = 0.0;
    double corr_h = 0.0;
    double npcr = 0.0;
    double uaci = 0.0;
    double seconds = 0.0;
};

struct SweepMetadata {
    std::string name;
    std::filesystem::path path;
    unsigned int symbol_bits = 0;
    double npcr_ideal = 0.0;
    double uaci_ideal = 0.0;
    std::size_t num_images = 0;
    std::vector<std::size_t> k_values;
};

struct NormalizedSweep {
    std::vector<NormalizedSweepRow> rows;
    SweepMetadata metadata;
};

NormalizedSweep read_sweep_csv(const std::filesystem::path& path,
                               std::string display_name = {});
void print_sweep_summary(const NormalizedSweep& sweep, std::ostream& output);
void compare_sweeps(const NormalizedSweep& rgb,
                    const NormalizedSweep& gray,
                    const std::filesystem::path& output_csv);

// Executes the assertions from test_xormap_gray_fast.m in native C++.
void verify_matlab_fast_path(std::ostream& progress);

}  // namespace xormap_image

#endif
