#include "test_support.hpp"
#include "xormap_image/plot.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

xormap_image::Panel basic_panel()
{
    xormap_image::Series line;
    line.label = "mean";
    line.points = {{8.0, 7.2}, {16.0, 7.8}, {24.0, 7.95}};
    line.marker_radius = 2.0;

    xormap_image::Series observations;
    observations.label = "observations";
    observations.points = {{8.0, 7.0}, {8.0, 7.4}, {16.0, 7.7},
                           {16.0, 7.9}, {24.0, 7.92}, {24.0, 7.98}};
    observations.color = {0.4, 0.4, 0.4, 0.35};
    observations.style = xormap_image::SeriesStyle::Scatter;
    observations.marker_radius = 1.3;

    xormap_image::Panel panel;
    panel.title = "Entropy sweep";
    panel.x_label = "K (state bits)";
    panel.y_label = "Entropy (bits)";
    panel.series = {std::move(observations), std::move(line)};
    panel.reference_lines.push_back(
        {8.0, "ideal", {0.25, 0.25, 0.25, 0.8},
         xormap_image::ReferenceOrientation::Horizontal, 1.0});
    panel.reference_lines.push_back(
        {16.0, "K=16", {0.45, 0.45, 0.45, 0.6},
         xormap_image::ReferenceOrientation::Vertical, 0.8});
    return panel;
}

xormap_image::AdjacentPixelPairs adjacent_pairs(
    const xormap_image::Image& image)
{
    xormap_image::AdjacentPixelPairs pairs;
    for (std::size_t y = 0U; y < image.height(); ++y) {
        for (std::size_t x = 0U; x + 1U < image.width(); ++x) {
            pairs.x.push_back(image.at(x, y));
            pairs.y.push_back(image.at(x + 1U, y));
        }
    }
    return pairs;
}

}  // namespace

TEST_CASE("Cairo plotting helpers write complete native PDF reports",
          "[plot][integration]")
{
    test_support::TemporaryDirectory temporary;
    const xormap_image::Image plain = test_support::patterned_image(1U);
    const xormap_image::Image cipher = test_support::patterned_image(5U);
    const auto plain_pairs = adjacent_pairs(plain);
    const auto cipher_pairs = adjacent_pairs(cipher);

    const std::filesystem::path generic =
        temporary.path() / "nested" / "generic.pdf";
    xormap_image::Panel bars = xormap_image::make_histogram_panel(
        "Histogram", plain.pixels(), {0.92, 0.41, 0.20, 0.85});
    xormap_image::write_plot_grid_pdf(
        generic, "Synthetic native report", "line, scatter, bars, and ideals",
        {basic_panel(), std::move(bars)}, 2U);

    const std::filesystem::path images = temporary.path() / "images.pdf";
    const std::filesystem::path histograms =
        temporary.path() / "histograms.pdf";
    const std::filesystem::path scatter = temporary.path() / "scatter.pdf";
    xormap_image::write_image_comparison_pdf(
        images, "Image comparison", plain, cipher, "16 x 16 synthetic data");
    xormap_image::write_histogram_comparison_pdf(
        histograms, "Histogram comparison", plain, cipher);
    xormap_image::write_adjacent_scatter_comparison_pdf(
        scatter, "Adjacent-pair comparison", plain_pairs, cipher_pairs);

    REQUIRE(test_support::has_pdf_signature(generic));
    REQUIRE(test_support::has_pdf_signature(images));
    REQUIRE(test_support::has_pdf_signature(histograms));
    REQUIRE(test_support::has_pdf_signature(scatter));

    const std::string image_pdf = test_support::read_text(images);
    REQUIRE(image_pdf.find("/Subtype /Image") != std::string::npos);
    REQUIRE(std::filesystem::is_directory(generic.parent_path()));
}

TEST_CASE("plot panels own image pixels independently of the caller",
          "[plot][ownership]")
{
    test_support::TemporaryDirectory temporary;
    xormap_image::Panel panel;
    {
        xormap_image::Image local = test_support::patterned_image(9U);
        panel = xormap_image::make_grayscale_image_panel("Owned copy", local);
        local.at(0U, 0U) ^= 0xFFU;
    }

    REQUIRE(panel.image.has_value());
    REQUIRE(panel.image->at(0U, 0U) ==
            test_support::patterned_image(9U).at(0U, 0U));
    const auto output = temporary.path() / "owned.pdf";
    xormap_image::write_plot_grid_pdf(output, "Ownership", {}, {panel}, 1U);
    REQUIRE(test_support::has_pdf_signature(output));
}

TEST_CASE("plot API rejects invalid dimensions, ranges, and samples",
          "[plot][errors]")
{
    test_support::TemporaryDirectory temporary;
    const auto output = temporary.path() / "invalid.pdf";
    const xormap_image::Panel valid = basic_panel();

    REQUIRE_THROWS_AS(xormap_image::write_plot_grid_pdf(
                          {}, "title", {}, {valid}, 1U),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::write_plot_grid_pdf(
                          output, "title", {}, {}, 1U),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::write_plot_grid_pdf(
                          output, "title", {}, {valid}, 0U),
                      std::invalid_argument);

    xormap_image::Panel non_finite = valid;
    non_finite.series.front().points.front().y =
        std::numeric_limits<double>::quiet_NaN();
    REQUIRE_THROWS_AS(xormap_image::write_plot_grid_pdf(
                          output, "title", {}, {non_finite}, 1U),
                      std::invalid_argument);

    xormap_image::Panel bad_color = valid;
    bad_color.series.front().color.alpha = 1.01;
    REQUIRE_THROWS_AS(xormap_image::write_plot_grid_pdf(
                          output, "title", {}, {bad_color}, 1U),
                      std::invalid_argument);

    xormap_image::Panel bad_range = valid;
    bad_range.x_range = xormap_image::AxisRange{2.0, 2.0};
    REQUIRE_THROWS_AS(xormap_image::write_plot_grid_pdf(
                          output, "title", {}, {bad_range}, 1U),
                      std::invalid_argument);

    xormap_image::Panel bad_reference = valid;
    bad_reference.reference_lines.front().value =
        std::numeric_limits<double>::infinity();
    REQUIRE_THROWS_AS(xormap_image::write_plot_grid_pdf(
                          output, "title", {}, {bad_reference}, 1U),
                      std::invalid_argument);

    xormap_image::Panel missing_image;
    missing_image.kind = xormap_image::PanelKind::GrayscaleImage;
    REQUIRE_THROWS_AS(xormap_image::write_plot_grid_pdf(
                          output, "title", {}, {missing_image}, 1U),
                      std::invalid_argument);

    REQUIRE_THROWS_AS(xormap_image::make_histogram_panel("empty", {}),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::make_grayscale_image_panel(
                          "empty", xormap_image::Image{}),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::make_adjacent_scatter_panel(
                          "mismatch", {{1U, 2U}, {3U}}),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::make_adjacent_scatter_panel(
                          "empty", {{}, {}}),
                      std::invalid_argument);
}

TEST_CASE("an explicitly ranged empty plot is a valid axes-only panel",
          "[plot][integration]")
{
    test_support::TemporaryDirectory temporary;
    xormap_image::Panel axes;
    axes.title = "No observations";
    axes.x_label = "x";
    axes.y_label = "y";
    axes.x_range = xormap_image::AxisRange{0.0, 1.0};
    axes.y_range = xormap_image::AxisRange{-1.0, 1.0};
    axes.reference_lines.push_back({0.0, "zero"});

    const auto output = temporary.path() / "axes-only.pdf";
    xormap_image::write_plot_grid_pdf(output, "Empty plot", {}, {axes}, 1U);
    REQUIRE(test_support::has_pdf_signature(output));
}
