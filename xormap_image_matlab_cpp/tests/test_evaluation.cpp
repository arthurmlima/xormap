#include "test_support.hpp"
#include "xormap_image/evaluation.hpp"
#include "xormap_image/image.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;

struct SyntheticCorpus {
    fs::path images;
    fs::path manifest;
    xormap_image::Image alpha;
    xormap_image::Image beta;
};

SyntheticCorpus make_corpus(const fs::path& root)
{
    SyntheticCorpus corpus;
    corpus.images = root / "images";
    corpus.manifest = corpus.images / "manifest_gray.csv";
    corpus.alpha = test_support::patterned_image(11U);
    corpus.beta = test_support::patterned_image(29U);
    fs::create_directories(corpus.images);

    xormap_image::write_tiff_gray8(corpus.images / "alpha.tiff", corpus.alpha);
    xormap_image::write_tiff_gray8(corpus.images / "beta.tiff", corpus.beta);

    // The three-image MATLAB workflow requires these exact SIPI filenames.
    xormap_image::write_tiff_gray8(corpus.images / "boat.512.tiff",
                                   test_support::patterned_image(1U));
    xormap_image::write_tiff_gray8(corpus.images / "5.1.09.tiff",
                                   test_support::patterned_image(2U));
    xormap_image::write_tiff_gray8(corpus.images / "7.1.01.tiff",
                                   test_support::patterned_image(3U));

    test_support::write_text(
        corpus.manifest,
        "filename,volume,name,height,width,channels\n"
        "alpha.tiff,vol-a,Alpha,16,16,1\n"
        "beta.tiff,vol-b,Beta,16,16,1\n");
    return corpus;
}

std::size_t unsigned_field(const std::string& value)
{
    std::size_t consumed = 0U;
    const unsigned long long parsed = std::stoull(value, &consumed, 10);
    if (consumed != value.size()) {
        throw std::invalid_argument("test field is not an unsigned integer");
    }
    return static_cast<std::size_t>(parsed);
}

void require_pdf_set(const fs::path& directory,
                     const std::vector<std::string>& names)
{
    for (const std::string& name : names) {
        INFO("checking " << name);
        REQUIRE(test_support::has_pdf_signature(directory / name));
    }
}

void require_equal_except_timing(
    const std::vector<std::vector<std::string>>& first,
    const std::vector<std::vector<std::string>>& second,
    std::size_t timing_column)
{
    REQUIRE(first.size() == second.size());
    for (std::size_t row = 0U; row < first.size(); ++row) {
        REQUIRE(first[row].size() == second[row].size());
        for (std::size_t column = 0U; column < first[row].size(); ++column) {
            if (row != 0U && column == timing_column) {
                continue;
            }
            CAPTURE(row, column);
            REQUIRE(first[row][column] == second[row][column]);
        }
    }
}

}  // namespace

TEST_CASE("run-all evaluates three synthetic TIFFs and preserves round trips",
          "[evaluation][run-all][integration]")
{
    test_support::TemporaryDirectory temporary;
    const SyntheticCorpus corpus = make_corpus(temporary.path());
    const fs::path results = temporary.path() / "run-all-results";
    std::ostringstream progress;

    xormap_image::RunAllOptions options;
    options.paths = {corpus.images, results};
    options.k = 13U;
    options.correlation_samples = 24U;
    options.scatter_samples = 12U;
    options.workers = 3U;
    xormap_image::run_all(options, progress);

    struct ExpectedImage {
        const char* tag;
        const char* source;
        const char* display_name;
    };
    constexpr std::array<ExpectedImage, 3> expected{{
        {"boat.512", "boat.512.tiff", "boat.512 (Fishing Boat, 512x512)"},
        {"5.1.09", "5.1.09.tiff", "5.1.09 (Moon Surface, 256x256)"},
        {"7.1.01", "7.1.01.tiff", "7.1.01 (Truck, 512x512)"},
    }};

    for (const ExpectedImage& image : expected) {
        CAPTURE(image.tag);
        const xormap_image::Image source =
            xormap_image::load_tiff_gray8(corpus.images / image.source);
        const xormap_image::Image plain = xormap_image::load_tiff_gray8(
            results / (std::string(image.tag) + "_plain.tiff"));
        const xormap_image::Image cipher = xormap_image::load_tiff_gray8(
            results / (std::string(image.tag) + "_cipher.tiff"));
        const xormap_image::Image recovered = xormap_image::load_tiff_gray8(
            results / (std::string(image.tag) + "_recovered.tiff"));
        REQUIRE(plain == source);
        REQUIRE(recovered == source);
        REQUIRE(cipher != source);

        const auto histogram = test_support::read_simple_csv(
            results / (std::string(image.tag) + "_histogram.csv"));
        REQUIRE(histogram.size() == 257U);
        REQUIRE(histogram.front().size() == 3U);
        REQUIRE(histogram.front()[0] == "gray_level");
        REQUIRE(histogram.front()[1] == "plain_count");
        REQUIRE(histogram.front()[2] == "cipher_count");
        std::size_t plain_total = 0U;
        std::size_t cipher_total = 0U;
        for (std::size_t row = 1U; row < histogram.size(); ++row) {
            REQUIRE(histogram[row].size() == 3U);
            REQUIRE(unsigned_field(histogram[row][0]) == row - 1U);
            plain_total += unsigned_field(histogram[row][1]);
            cipher_total += unsigned_field(histogram[row][2]);
        }
        REQUIRE(plain_total == source.size());
        REQUIRE(cipher_total == source.size());

        const auto scatter = test_support::read_simple_csv(
            results / (std::string(image.tag) + "_correlation.csv"));
        REQUIRE(scatter.size() == options.scatter_samples + 1U);
        REQUIRE(scatter.front().size() == 5U);
        REQUIRE(scatter.front()[0] == "sample");
        for (std::size_t row = 1U; row < scatter.size(); ++row) {
            REQUIRE(scatter[row].size() == 5U);
            REQUIRE(unsigned_field(scatter[row][0]) == row);
        }

        require_pdf_set(results,
                        {std::string(image.tag) + "_images.pdf",
                         std::string(image.tag) + "_histogram.pdf",
                         std::string(image.tag) + "_correlation.pdf"});
    }

    const std::string markdown = test_support::read_text(results / "results.md");
    std::size_t previous = 0U;
    for (const ExpectedImage& image : expected) {
        const std::size_t position = markdown.find(image.display_name, previous);
        REQUIRE(position != std::string::npos);
        previous = position + 1U;
    }
    REQUIRE(progress.str().find("run-all images: 3/3 complete") !=
            std::string::npos);
    REQUIRE(progress.str().find("Round-trip OK") != std::string::npos);
}

TEST_CASE("three-image sweep keeps MATLAB image-major row order",
          "[evaluation][sweep][integration]")
{
    test_support::TemporaryDirectory temporary;
    const SyntheticCorpus corpus = make_corpus(temporary.path());
    const fs::path results = temporary.path() / "three-sweep";
    std::ostringstream progress;

    xormap_image::SweepOptions options;
    options.paths = {corpus.images, results};
    options.k_values = {8U, 13U};
    options.correlation_samples = 20U;
    options.workers = 4U;
    xormap_image::sweep_three_images(options, progress);

    const auto rows = test_support::read_simple_csv(results / "sweep_k.csv");
    REQUIRE(rows.size() == 7U);
    REQUIRE(test_support::read_lines(results / "sweep_k.csv").front() ==
            "image,K,entropy_cipher,corrH_cipher,corrV_cipher,corrD_cipher,"
            "npcr,uaci,seconds");
    constexpr std::array<const char*, 6> images{{
        "boat.512", "boat.512", "5.1.09", "5.1.09", "7.1.01", "7.1.01"}};
    constexpr std::array<std::size_t, 6> keys{{8U, 13U, 8U, 13U, 8U, 13U}};
    for (std::size_t index = 0U; index < images.size(); ++index) {
        REQUIRE(rows[index + 1U].size() == 9U);
        REQUIRE(rows[index + 1U][0] == images[index]);
        REQUIRE(unsigned_field(rows[index + 1U][1]) == keys[index]);
    }
    REQUIRE(test_support::has_pdf_signature(results / "sweep_k.pdf"));
    REQUIRE(progress.str().find("3 images x 2 K values = 6") !=
            std::string::npos);
    REQUIRE(progress.str().find("three-image sweep: 6/6 complete") !=
            std::string::npos);
}

TEST_CASE("all-image sweeps are schedule-invariant and feed comparison reports",
          "[evaluation][sweep][compare][parallel][integration]")
{
    test_support::TemporaryDirectory temporary;
    const SyntheticCorpus corpus = make_corpus(temporary.path());
    const fs::path sequential_results = temporary.path() / "sequential";
    const fs::path parallel_results = temporary.path() / "parallel";

    xormap_image::SweepOptions options;
    options.paths.images_directory = corpus.images;
    options.manifest_path = corpus.manifest;
    options.k_values = {8U, 13U};
    options.correlation_samples = 24U;

    std::ostringstream sequential_progress;
    options.paths.results_directory = sequential_results;
    options.workers = 1U;
    xormap_image::sweep_all_images(options, sequential_progress);

    std::ostringstream parallel_progress;
    options.paths.results_directory = parallel_results;
    options.workers = 4U;
    xormap_image::sweep_all_images(options, parallel_progress);

    const auto sequential = test_support::read_simple_csv(
        sequential_results / "sweep_k_gray_all.csv");
    const auto parallel = test_support::read_simple_csv(
        parallel_results / "sweep_k_gray_all.csv");
    REQUIRE(sequential.size() == 5U);
    REQUIRE(parallel.size() == 5U);
    REQUIRE(sequential.front().size() == 13U);
    REQUIRE(test_support::read_lines(
                parallel_results / "sweep_k_gray_all.csv").front() ==
            "image,volume,K,height,width,num_pixels,iterations_per_encrypt,"
            "pixels_per_iteration,entropy_cipher,abs_corrH_cipher,npcr,uaci,"
            "seconds_two_encryptions");
    require_equal_except_timing(sequential, parallel, 12U);

    constexpr std::array<const char*, 4> expected_names{{
        "Alpha", "Beta", "Alpha", "Beta"}};
    constexpr std::array<std::size_t, 4> expected_keys{{8U, 8U, 13U, 13U}};
    constexpr std::array<std::size_t, 4> expected_iterations{{
        256U, 256U, 158U, 158U}};
    for (std::size_t index = 0U; index < expected_names.size(); ++index) {
        const auto& row = parallel[index + 1U];
        REQUIRE(row.size() == 13U);
        REQUIRE(row[0] == expected_names[index]);
        REQUIRE(unsigned_field(row[2]) == expected_keys[index]);
        REQUIRE(unsigned_field(row[3]) == 16U);
        REQUIRE(unsigned_field(row[4]) == 16U);
        REQUIRE(unsigned_field(row[5]) == 256U);
        REQUIRE(unsigned_field(row[6]) == expected_iterations[index]);
    }
    REQUIRE(test_support::has_pdf_signature(
        sequential_results / "sweep_k_gray_all.pdf"));
    REQUIRE(test_support::has_pdf_signature(
        parallel_results / "sweep_k_gray_all.pdf"));
    REQUIRE(parallel_progress.str().find("all-image sweep: 4/4 complete") !=
            std::string::npos);

    const xormap_image::NormalizedSweep gray = xormap_image::read_sweep_csv(
        parallel_results / "sweep_k_gray_all.csv", "Synthetic grayscale");
    REQUIRE(gray.rows.size() == 4U);
    REQUIRE(gray.metadata.name == "Synthetic grayscale");
    REQUIRE(gray.metadata.symbol_bits == 8U);
    REQUIRE(gray.metadata.num_images == 2U);
    REQUIRE((gray.metadata.k_values == std::vector<std::size_t>{8U, 13U}));
    REQUIRE(gray.rows[0].k == 8U);
    REQUIRE(gray.rows[0].image == "Alpha");
    REQUIRE(gray.rows[1].image == "Beta");

    std::ostringstream summary;
    xormap_image::print_sweep_summary(gray, summary);
    REQUIRE(summary.str().find("2 images x 2 K values = 4 rows") !=
            std::string::npos);
    REQUIRE(summary.str().find("8-bit symbols") != std::string::npos);

    const fs::path rgb_path = temporary.path() / "rgb_sweep.csv";
    test_support::write_text(
        rgb_path,
        "image,K,height,width,num_pixels,mean_entropy_cipher,"
        "mean_abs_corrH_cipher,npcr_packed,uaci_packed,seconds\n"
        "RGB-B,13,16,16,256,7.91,0.012,99.61,33.42,0.2\n"
        "RGB-A,8,16,16,256,7.82,0.025,99.55,33.31,0.1\n"
        "RGB-A,13,16,16,256,7.96,0.009,99.62,33.47,0.2\n"
        "RGB-B,8,16,16,256,7.80,0.030,99.50,33.20,0.1\n");
    const xormap_image::NormalizedSweep rgb =
        xormap_image::read_sweep_csv(rgb_path, "Synthetic RGB888");
    REQUIRE(rgb.metadata.symbol_bits == 24U);
    REQUIRE(rgb.metadata.num_images == 2U);
    REQUIRE(rgb.rows.front().k == 8U);
    REQUIRE(rgb.rows.front().image == "RGB-A");

    const fs::path comparison =
        temporary.path() / "comparison" / "summary.csv";
    xormap_image::compare_sweeps(rgb, gray, comparison);
    const auto compared = test_support::read_simple_csv(comparison);
    REQUIRE(compared.size() == 3U);
    REQUIRE(compared.front().size() == 21U);
    REQUIRE(compared[1U].size() == 21U);
    REQUIRE(unsigned_field(compared[1U][0]) == 8U);
    REQUIRE(unsigned_field(compared[2U][0]) == 13U);
    REQUIRE(unsigned_field(compared[1U][1]) == 2U);
    REQUIRE(unsigned_field(compared[1U][2]) == 2U);
    require_pdf_set(comparison.parent_path(),
                    {"compare_rgb_gray_entropy.pdf",
                     "compare_rgb_gray_correlation.pdf",
                     "compare_rgb_gray_npcr_uaci.pdf"});

    xormap_image::NormalizedSweep mismatched = gray;
    mismatched.rows.erase(mismatched.rows.begin() + 2,
                          mismatched.rows.end());
    REQUIRE_THROWS_AS(xormap_image::compare_sweeps(
                          rgb, mismatched,
                          temporary.path() / "must-not-exist.csv"),
                      std::invalid_argument);
}

TEST_CASE("analysis emits all MATLAB CSV schemas in deterministic K-major order",
          "[evaluation][analysis][parallel][integration]")
{
    test_support::TemporaryDirectory temporary;
    const SyntheticCorpus corpus = make_corpus(temporary.path());
    const fs::path results = temporary.path() / "analysis";
    std::ostringstream progress;

    xormap_image::AnalysisOptions options;
    options.paths = {corpus.images, results};
    options.manifest_path = corpus.manifest;
    options.k_values = {8U, 13U};
    options.workers = 4U;
    xormap_image::analyze_all_images(options, progress);

    const auto key = test_support::read_simple_csv(
        results / "key_sensitivity_gray.csv");
    REQUIRE(key.size() == 5U);
    REQUIRE(test_support::read_lines(
                results / "key_sensitivity_gray.csv").front() ==
            "image,volume,K,num_pixels,flipped_key_bit,npcr_key,uaci_key,"
            "psnr_cipher_pair_db,psnr_plain_wrongkey_db,first_differing_byte,"
            "key_diff_checksum");
    constexpr std::array<const char*, 4> names{{
        "Alpha", "Beta", "Alpha", "Beta"}};
    constexpr std::array<std::size_t, 4> keys{{8U, 8U, 13U, 13U}};
    for (std::size_t index = 0U; index < names.size(); ++index) {
        REQUIRE(key[index + 1U].size() == 11U);
        REQUIRE(key[index + 1U][0] == names[index]);
        REQUIRE(unsigned_field(key[index + 1U][2]) == keys[index]);
        REQUIRE(unsigned_field(key[index + 1U][3]) == 256U);
        REQUIRE(unsigned_field(key[index + 1U][4]) == 1U);
        REQUIRE(unsigned_field(key[index + 1U][9]) > 0U);
    }
    REQUIRE(key[1U][10] == key[2U][10]);
    REQUIRE(key[3U][10] == key[4U][10]);

    const auto bits = test_support::read_simple_csv(
        results / "key_sensitivity_bits_gray.csv");
    REQUIRE(bits.size() == 22U);
    REQUIRE(bits.front().size() == 5U);
    for (std::size_t bit = 1U; bit <= 8U; ++bit) {
        REQUIRE(unsigned_field(bits[bit][0]) == 8U);
        REQUIRE(unsigned_field(bits[bit][1]) == bit);
    }
    for (std::size_t bit = 1U; bit <= 13U; ++bit) {
        REQUIRE(unsigned_field(bits[8U + bit][0]) == 13U);
        REQUIRE(unsigned_field(bits[8U + bit][1]) == bit);
    }

    const auto histogram = test_support::read_simple_csv(
        results / "histogram_analysis_gray.csv");
    REQUIRE(histogram.size() == 5U);
    REQUIRE(histogram.front().size() == 8U);
    for (std::size_t row = 1U; row < histogram.size(); ++row) {
        REQUIRE(histogram[row].size() == 8U);
        REQUIRE((histogram[row][7] == "0" || histogram[row][7] == "1"));
    }

    const auto psnr = test_support::read_simple_csv(
        results / "psnr_analysis_gray.csv");
    REQUIRE(psnr.size() == 5U);
    REQUIRE(psnr.front().size() == 7U);
    for (std::size_t row = 1U; row < psnr.size(); ++row) {
        REQUIRE(psnr[row].size() == 7U);
        REQUIRE(psnr[row][6] == "Inf");
    }

    require_pdf_set(results,
                    {"key_sensitivity_gray.pdf",
                     "histogram_analysis_gray.pdf",
                     "psnr_analysis_gray.pdf"});
    REQUIRE(progress.str().find("analysis main pass: 4/4 complete") !=
            std::string::npos);
    REQUIRE(progress.str().find("per-bit study: 21/21 complete") !=
            std::string::npos);
    REQUIRE(progress.str().find("Round-trip PSNR is +Inf in 4 of 4 cases") !=
            std::string::npos);
}

TEST_CASE("sweep readers reject malformed schemas and inconsistent dimensions",
          "[evaluation][read-sweep][errors]")
{
    test_support::TemporaryDirectory temporary;
    const fs::path malformed = temporary.path() / "malformed.csv";
    test_support::write_text(malformed, "image,K\na,8\n");
    REQUIRE_THROWS_AS(xormap_image::read_sweep_csv(malformed),
                      std::runtime_error);

    const fs::path dimensions = temporary.path() / "dimensions.csv";
    test_support::write_text(
        dimensions,
        "image,volume,K,height,width,num_pixels,entropy_cipher,"
        "abs_corrH_cipher,npcr,uaci,seconds_two_encryptions\n"
        "Alpha,vol-a,8,16,16,255,7.9,0.01,99.6,33.4,0.1\n");
    REQUIRE_THROWS_AS(xormap_image::read_sweep_csv(dimensions),
                      std::runtime_error);

    std::ostringstream empty_summary;
    REQUIRE_THROWS_AS(xormap_image::print_sweep_summary(
                          xormap_image::NormalizedSweep{}, empty_summary),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::compare_sweeps(
                          xormap_image::NormalizedSweep{},
                          xormap_image::NormalizedSweep{},
                          temporary.path() / "empty.csv"),
                      std::invalid_argument);

    REQUIRE((xormap_image::make_k_values(8U, 4U, 17U) ==
             std::vector<std::size_t>{8U, 12U, 16U}));
    REQUIRE(xormap_image::make_k_values(12U, 4U, 8U).empty());
    REQUIRE_THROWS_AS(xormap_image::make_k_values(8U, 0U, 16U),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::make_k_values(4U, 1U, 16U),
                      std::invalid_argument);
}
