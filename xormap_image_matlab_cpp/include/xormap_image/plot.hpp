#ifndef XORMAP_IMAGE_PLOT_HPP
#define XORMAP_IMAGE_PLOT_HPP

#include "xormap_image/image.hpp"
#include "xormap_image/metrics.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace xormap_image {

// Components are in Cairo's normalized [0, 1] colour space.
struct Color {
    double red = 0.165;
    double green = 0.471;
    double blue = 0.839;
    double alpha = 1.0;
};

struct Point {
    double x = 0.0;
    double y = 0.0;
};

enum class SeriesStyle {
    Line,
    Scatter,
    Bars,
};

struct Series {
    std::string label;
    std::vector<Point> points;
    Color color;
    SeriesStyle style = SeriesStyle::Line;
    double line_width = 1.5;
    // A positive radius adds markers to a line and controls scatter size.
    double marker_radius = 0.0;
};

struct AxisRange {
    double minimum = 0.0;
    double maximum = 1.0;
};

enum class ReferenceOrientation {
    Horizontal,
    Vertical,
};

struct ReferenceLine {
    double value = 0.0;
    std::string label = "ideal";
    Color color{0.35, 0.35, 0.35, 0.8};
    ReferenceOrientation orientation = ReferenceOrientation::Horizontal;
    double line_width = 1.0;
};

enum class PanelKind {
    Plot,
    GrayscaleImage,
};

struct Panel {
    PanelKind kind = PanelKind::Plot;
    std::string title;
    std::string x_label;
    std::string y_label;
    std::vector<Series> series;
    std::vector<ReferenceLine> reference_lines;
    std::optional<AxisRange> x_range;
    std::optional<AxisRange> y_range;
    bool show_grid = true;
    bool show_legend = true;
    bool square_axes = false;

    // Image panels own their pixels. This deliberately avoids lifetime hazards
    // when a set of panels is assembled before it is rendered.
    std::optional<Image> image;
};

using PlotPanel = Panel;

// Writes one PDF page. Plot lines, bars, markers, text, axes, and grids are
// native PDF vector primitives; grayscale panels are embedded pixel surfaces.
// Missing parent directories are created. Invalid/non-finite input and Cairo
// output errors are reported with exceptions.
void write_plot_grid_pdf(const std::filesystem::path& path,
                         std::string_view title,
                         std::string_view subtitle,
                         const std::vector<Panel>& panels,
                         std::size_t columns = 2);

[[nodiscard]] Panel make_grayscale_image_panel(std::string title,
                                                const Image& image);
[[nodiscard]] Panel make_histogram_panel(
    std::string title,
    const std::vector<std::uint8_t>& bytes,
    Color color = Color{0.165, 0.471, 0.839, 0.9});
[[nodiscard]] Panel make_adjacent_scatter_panel(
    std::string title,
    const AdjacentPixelPairs& pairs,
    Color color = Color{0.165, 0.471, 0.839, 0.45});

void write_image_comparison_pdf(const std::filesystem::path& path,
                                std::string_view title,
                                const Image& plain,
                                const Image& cipher,
                                std::string_view subtitle = {});

void write_histogram_comparison_pdf(
    const std::filesystem::path& path,
    std::string_view title,
    const std::vector<std::uint8_t>& plain,
    const std::vector<std::uint8_t>& cipher,
    std::string_view subtitle = {});

void write_histogram_comparison_pdf(const std::filesystem::path& path,
                                    std::string_view title,
                                    const Image& plain,
                                    const Image& cipher,
                                    std::string_view subtitle = {});

void write_adjacent_scatter_comparison_pdf(
    const std::filesystem::path& path,
    std::string_view title,
    const AdjacentPixelPairs& plain,
    const AdjacentPixelPairs& cipher,
    std::string_view subtitle = {});

}  // namespace xormap_image

#endif  // XORMAP_IMAGE_PLOT_HPP
