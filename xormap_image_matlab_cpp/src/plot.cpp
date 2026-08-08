#include "xormap_image/plot.hpp"

#include <cairo-pdf.h>
#include <cairo.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace xormap_image {
namespace {

constexpr double kPageMargin = 34.0;
constexpr double kPanelGap = 18.0;
constexpr double kPanelWidth = 410.0;
constexpr double kPanelHeight = 300.0;
constexpr double kMaximumPageDimension = 14'400.0;
constexpr double kPi = 3.14159265358979323846;

struct Rect {
    double x;
    double y;
    double width;
    double height;
};

struct Bounds {
    double x_min;
    double x_max;
    double y_min;
    double y_max;
};

using Surface = std::unique_ptr<cairo_surface_t, decltype(&cairo_surface_destroy)>;
using Context = std::unique_ptr<cairo_t, decltype(&cairo_destroy)>;
using Pattern = std::unique_ptr<cairo_pattern_t, decltype(&cairo_pattern_destroy)>;

[[noreturn]] void throw_cairo_error(const std::filesystem::path& path,
                                    std::string_view action,
                                    cairo_status_t status)
{
    throw std::runtime_error("PDF '" + path.string() + "': " +
                             std::string(action) + ": " +
                             cairo_status_to_string(status));
}

void check_context(cairo_t* context, const std::filesystem::path& path,
                   std::string_view action)
{
    const cairo_status_t status = cairo_status(context);
    if (status != CAIRO_STATUS_SUCCESS) {
        throw_cairo_error(path, action, status);
    }
}

void check_surface(cairo_surface_t* surface, const std::filesystem::path& path,
                   std::string_view action)
{
    const cairo_status_t status = cairo_surface_status(surface);
    if (status != CAIRO_STATUS_SUCCESS) {
        throw_cairo_error(path, action, status);
    }
}

[[nodiscard]] bool finite(double value) noexcept
{
    return std::isfinite(value) != 0;
}

void validate_color(const Color& color, std::string_view description)
{
    const std::array<double, 4> components{
        color.red, color.green, color.blue, color.alpha};
    for (const double component : components) {
        if (!finite(component) || component < 0.0 || component > 1.0) {
            throw std::invalid_argument(std::string(description) +
                                        " colour components must be finite and "
                                        "in [0, 1]");
        }
    }
}

void validate_range(const AxisRange& range, std::string_view description)
{
    if (!finite(range.minimum) || !finite(range.maximum) ||
        range.minimum >= range.maximum) {
        throw std::invalid_argument(std::string(description) +
                                    " range must have finite minimum < maximum");
    }
}

void validate_panel(const Panel& panel, std::size_t index)
{
    const std::string prefix = "panel " + std::to_string(index + 1);
    if (panel.x_range.has_value()) {
        validate_range(*panel.x_range, prefix + " x-axis");
    }
    if (panel.y_range.has_value()) {
        validate_range(*panel.y_range, prefix + " y-axis");
    }

    if (panel.kind == PanelKind::GrayscaleImage) {
        if (!panel.image.has_value()) {
            throw std::invalid_argument(prefix +
                                        " is an image panel without an image");
        }
        panel.image->validate();
        if (panel.image->width() >
                static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
            panel.image->height() >
                static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            throw std::invalid_argument(prefix +
                                        " image dimensions exceed Cairo limits");
        }
        return;
    }

    bool has_point = false;
    for (std::size_t series_index = 0; series_index < panel.series.size();
         ++series_index) {
        const Series& series = panel.series[series_index];
        const std::string series_prefix =
            prefix + ", series " + std::to_string(series_index + 1);
        validate_color(series.color, series_prefix);
        if (!finite(series.line_width) || series.line_width <= 0.0) {
            throw std::invalid_argument(series_prefix +
                                        " line width must be finite and positive");
        }
        if (!finite(series.marker_radius) || series.marker_radius < 0.0) {
            throw std::invalid_argument(series_prefix +
                                        " marker radius must be finite and non-negative");
        }
        for (const Point& point : series.points) {
            if (!finite(point.x) || !finite(point.y)) {
                throw std::invalid_argument(series_prefix +
                                            " contains a non-finite point");
            }
            has_point = true;
        }
    }

    for (std::size_t line_index = 0;
         line_index < panel.reference_lines.size(); ++line_index) {
        const ReferenceLine& line = panel.reference_lines[line_index];
        const std::string line_prefix =
            prefix + ", reference line " + std::to_string(line_index + 1);
        if (!finite(line.value)) {
            throw std::invalid_argument(line_prefix +
                                        " value must be finite");
        }
        validate_color(line.color, line_prefix);
        if (!finite(line.line_width) || line.line_width <= 0.0) {
            throw std::invalid_argument(line_prefix +
                                        " width must be finite and positive");
        }
    }

    if (!has_point &&
        !(panel.x_range.has_value() && panel.y_range.has_value())) {
        throw std::invalid_argument(prefix +
                                    " has no points and no complete axis ranges");
    }
}

void set_source(cairo_t* context, const Color& color)
{
    cairo_set_source_rgba(context, color.red, color.green, color.blue,
                          color.alpha);
}

enum class TextAlign {
    Left,
    Center,
    Right,
};

void select_font(cairo_t* context, double size, bool bold)
{
    cairo_select_font_face(context, "Sans", CAIRO_FONT_SLANT_NORMAL,
                           bold ? CAIRO_FONT_WEIGHT_BOLD
                                : CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(context, size);
}

[[nodiscard]] double fitted_font_size(cairo_t* context,
                                      const std::string& text,
                                      double requested,
                                      double maximum_width)
{
    double size = requested;
    cairo_text_extents_t extents{};
    cairo_text_extents(context, text.c_str(), &extents);
    if (extents.width > maximum_width && extents.width > 0.0) {
        size *= maximum_width / extents.width;
        size = std::max(6.0, size);
        cairo_set_font_size(context, size);
    }
    return size;
}

void draw_text(cairo_t* context, const std::string& text, double x,
               double baseline, TextAlign alignment)
{
    cairo_text_extents_t extents{};
    cairo_text_extents(context, text.c_str(), &extents);
    double origin_x = x - extents.x_bearing;
    if (alignment == TextAlign::Center) {
        origin_x -= extents.width / 2.0;
    } else if (alignment == TextAlign::Right) {
        origin_x -= extents.width;
    }
    cairo_move_to(context, origin_x, baseline);
    cairo_show_text(context, text.c_str());
}

void draw_fitted_text(cairo_t* context, const std::string& text, double x,
                      double baseline, double maximum_width,
                      double requested_size, bool bold, TextAlign alignment)
{
    cairo_save(context);
    select_font(context, requested_size, bold);
    static_cast<void>(
        fitted_font_size(context, text, requested_size, maximum_width));
    draw_text(context, text, x, baseline, alignment);
    cairo_restore(context);
}

[[nodiscard]] std::string format_tick(double value, double step)
{
    if (std::abs(value) < std::abs(step) * 1.0e-8) {
        value = 0.0;
    }
    std::ostringstream stream;
    const double magnitude = std::abs(value);
    if ((magnitude >= 100'000.0) ||
        (magnitude > 0.0 && magnitude < 0.001)) {
        stream << std::scientific << std::setprecision(1) << value;
        return stream.str();
    }

    int precision = 0;
    if (std::abs(step) < 1.0) {
        precision = static_cast<int>(
            std::min(6.0, std::ceil(-std::log10(std::abs(step))) + 1.0));
    }
    stream << std::fixed << std::setprecision(precision) << value;
    return stream.str();
}

[[nodiscard]] double nice_step(double range, int desired_intervals)
{
    const double rough = range / static_cast<double>(desired_intervals);
    const double exponent = std::floor(std::log10(rough));
    const double power = std::pow(10.0, exponent);
    const double fraction = rough / power;
    double nice_fraction = 10.0;
    if (fraction <= 1.0) {
        nice_fraction = 1.0;
    } else if (fraction <= 2.0) {
        nice_fraction = 2.0;
    } else if (fraction <= 5.0) {
        nice_fraction = 5.0;
    }
    return nice_fraction * power;
}

[[nodiscard]] std::vector<double> make_ticks(double minimum, double maximum)
{
    const double step = nice_step(maximum - minimum, 5);
    const double first = std::ceil(minimum / step) * step;
    std::vector<double> ticks;
    ticks.reserve(8);
    for (double value = first;
         value <= maximum + step * 1.0e-9 && ticks.size() < 100;
         value += step) {
        ticks.push_back(value);
    }
    return ticks;
}

[[nodiscard]] AxisRange expanded_range(double minimum, double maximum,
                                       bool include_zero)
{
    if (include_zero) {
        minimum = std::min(minimum, 0.0);
        maximum = std::max(maximum, 0.0);
    }
    if (minimum == maximum) {
        const double padding = std::max(1.0, std::abs(minimum) * 0.05);
        return {minimum - padding, maximum + padding};
    }
    const double padding = (maximum - minimum) * 0.05;
    return {minimum - padding, maximum + padding};
}

[[nodiscard]] Bounds find_bounds(const Panel& panel)
{
    double x_min = std::numeric_limits<double>::infinity();
    double x_max = -std::numeric_limits<double>::infinity();
    double y_min = std::numeric_limits<double>::infinity();
    double y_max = -std::numeric_limits<double>::infinity();
    bool has_bars = false;

    for (const Series& series : panel.series) {
        has_bars = has_bars || series.style == SeriesStyle::Bars;
        for (const Point& point : series.points) {
            x_min = std::min(x_min, point.x);
            x_max = std::max(x_max, point.x);
            y_min = std::min(y_min, point.y);
            y_max = std::max(y_max, point.y);
        }
    }
    for (const ReferenceLine& line : panel.reference_lines) {
        if (line.orientation == ReferenceOrientation::Horizontal) {
            y_min = std::min(y_min, line.value);
            y_max = std::max(y_max, line.value);
        } else {
            x_min = std::min(x_min, line.value);
            x_max = std::max(x_max, line.value);
        }
    }

    AxisRange x = panel.x_range.value_or(expanded_range(x_min, x_max, false));
    AxisRange y = panel.y_range.value_or(expanded_range(y_min, y_max, has_bars));
    return {x.minimum, x.maximum, y.minimum, y.maximum};
}

[[nodiscard]] double map_x(double value, const Rect& plot,
                           const Bounds& bounds)
{
    return plot.x + (value - bounds.x_min) /
                        (bounds.x_max - bounds.x_min) * plot.width;
}

[[nodiscard]] double map_y(double value, const Rect& plot,
                           const Bounds& bounds)
{
    return plot.y + plot.height - (value - bounds.y_min) /
                                      (bounds.y_max - bounds.y_min) *
                                      plot.height;
}

void draw_plot_axes(cairo_t* context, const Rect& plot, const Bounds& bounds,
                    const Panel& panel)
{
    const std::vector<double> x_ticks = make_ticks(bounds.x_min, bounds.x_max);
    const std::vector<double> y_ticks = make_ticks(bounds.y_min, bounds.y_max);
    const double x_step = nice_step(bounds.x_max - bounds.x_min, 5);
    const double y_step = nice_step(bounds.y_max - bounds.y_min, 5);

    cairo_set_line_width(context, 0.6);
    select_font(context, 8.0, false);
    for (const double value : x_ticks) {
        const double x = map_x(value, plot, bounds);
        if (panel.show_grid) {
            cairo_set_source_rgb(context, 0.91, 0.92, 0.93);
            cairo_move_to(context, x, plot.y);
            cairo_line_to(context, x, plot.y + plot.height);
            cairo_stroke(context);
        }
        cairo_set_source_rgb(context, 0.20, 0.22, 0.25);
        cairo_move_to(context, x, plot.y + plot.height);
        cairo_line_to(context, x, plot.y + plot.height + 4.0);
        cairo_stroke(context);
        draw_text(context, format_tick(value, x_step), x,
                  plot.y + plot.height + 15.0, TextAlign::Center);
    }
    for (const double value : y_ticks) {
        const double y = map_y(value, plot, bounds);
        if (panel.show_grid) {
            cairo_set_source_rgb(context, 0.91, 0.92, 0.93);
            cairo_move_to(context, plot.x, y);
            cairo_line_to(context, plot.x + plot.width, y);
            cairo_stroke(context);
        }
        cairo_set_source_rgb(context, 0.20, 0.22, 0.25);
        cairo_move_to(context, plot.x - 4.0, y);
        cairo_line_to(context, plot.x, y);
        cairo_stroke(context);
        draw_text(context, format_tick(value, y_step), plot.x - 7.0,
                  y + 3.0, TextAlign::Right);
    }

    cairo_set_source_rgb(context, 0.20, 0.22, 0.25);
    cairo_set_line_width(context, 0.8);
    cairo_rectangle(context, plot.x, plot.y, plot.width, plot.height);
    cairo_stroke(context);

    select_font(context, 9.5, false);
    draw_text(context, panel.x_label, plot.x + plot.width / 2.0,
              plot.y + plot.height + 34.0, TextAlign::Center);

    cairo_save(context);
    cairo_translate(context, plot.x - 43.0, plot.y + plot.height / 2.0);
    cairo_rotate(context, -kPi / 2.0);
    draw_text(context, panel.y_label, 0.0, 0.0, TextAlign::Center);
    cairo_restore(context);
}

void draw_reference_lines(cairo_t* context, const Rect& plot,
                          const Bounds& bounds, const Panel& panel)
{
    constexpr std::array<double, 2> dash{5.0, 3.0};
    for (const ReferenceLine& line : panel.reference_lines) {
        set_source(context, line.color);
        cairo_set_line_width(context, line.line_width);
        cairo_set_dash(context, dash.data(), static_cast<int>(dash.size()), 0.0);

        // Legends occupy the upper-right corner. Put horizontal reference
        // labels on the opposite side in legend-bearing panels so labels such
        // as "ideal" are never hidden behind the legend box.
        double label_x = panel.show_legend ? plot.x + 4.0
                                           : plot.x + plot.width - 4.0;
        double label_y;
        TextAlign label_alignment = panel.show_legend ? TextAlign::Left
                                                      : TextAlign::Right;
        if (line.orientation == ReferenceOrientation::Horizontal) {
            const double y = map_y(line.value, plot, bounds);
            cairo_move_to(context, plot.x, y);
            cairo_line_to(context, plot.x + plot.width, y);
            cairo_stroke(context);
            label_y = std::clamp(y - 3.0, plot.y + 9.0,
                                 plot.y + plot.height - 3.0);
        } else {
            const double x = map_x(line.value, plot, bounds);
            cairo_move_to(context, x, plot.y);
            cairo_line_to(context, x, plot.y + plot.height);
            cairo_stroke(context);
            label_x = std::clamp(x + 3.0, plot.x + 3.0,
                                 plot.x + plot.width - 35.0);
            label_y = plot.y + 11.0;
            label_alignment = TextAlign::Left;
        }
        cairo_set_dash(context, nullptr, 0, 0.0);
        if (!line.label.empty()) {
            select_font(context, 7.5, false);
            draw_text(context, line.label, label_x, label_y, label_alignment);
        }
    }
    cairo_set_dash(context, nullptr, 0, 0.0);
}

[[nodiscard]] double bar_width(const Series& series, const Bounds& bounds)
{
    if (series.points.size() < 2) {
        return (bounds.x_max - bounds.x_min) * 0.02;
    }
    std::vector<double> positions;
    positions.reserve(series.points.size());
    for (const Point& point : series.points) {
        positions.push_back(point.x);
    }
    std::sort(positions.begin(), positions.end());
    double spacing = std::numeric_limits<double>::infinity();
    for (std::size_t index = 1; index < positions.size(); ++index) {
        const double difference = positions[index] - positions[index - 1];
        if (difference > 0.0) {
            spacing = std::min(spacing, difference);
        }
    }
    if (!finite(spacing)) {
        spacing = (bounds.x_max - bounds.x_min) * 0.02;
    }
    return spacing * 0.9;
}

void draw_series(cairo_t* context, const Rect& plot, const Bounds& bounds,
                 const Series& series)
{
    if (series.points.empty()) {
        return;
    }
    set_source(context, series.color);
    cairo_set_line_width(context, series.line_width);
    cairo_set_line_join(context, CAIRO_LINE_JOIN_ROUND);
    cairo_set_line_cap(context, CAIRO_LINE_CAP_ROUND);

    if (series.style == SeriesStyle::Bars) {
        const double width = bar_width(series, bounds);
        const double baseline = map_y(0.0, plot, bounds);
        for (const Point& point : series.points) {
            const double left = map_x(point.x - width / 2.0, plot, bounds);
            const double right = map_x(point.x + width / 2.0, plot, bounds);
            const double value_y = map_y(point.y, plot, bounds);
            cairo_rectangle(context, left, std::min(value_y, baseline),
                            right - left, std::abs(value_y - baseline));
        }
        cairo_fill(context);
        return;
    }

    if (series.style == SeriesStyle::Line) {
        bool first = true;
        for (const Point& point : series.points) {
            const double x = map_x(point.x, plot, bounds);
            const double y = map_y(point.y, plot, bounds);
            if (first) {
                cairo_move_to(context, x, y);
                first = false;
            } else {
                cairo_line_to(context, x, y);
            }
        }
        cairo_stroke(context);
    }

    const double radius = series.style == SeriesStyle::Scatter
                              ? (series.marker_radius > 0.0
                                     ? series.marker_radius
                                     : 1.5)
                              : series.marker_radius;
    if (radius > 0.0) {
        for (const Point& point : series.points) {
            cairo_arc(context, map_x(point.x, plot, bounds),
                      map_y(point.y, plot, bounds), radius, 0.0, 2.0 * kPi);
            cairo_fill(context);
        }
    }
}

void draw_legend(cairo_t* context, const Rect& plot, const Panel& panel)
{
    std::vector<const Series*> entries;
    for (const Series& series : panel.series) {
        if (!series.label.empty()) {
            entries.push_back(&series);
        }
    }
    if (!panel.show_legend || entries.empty()) {
        return;
    }

    select_font(context, 7.5, false);
    double text_width = 0.0;
    for (const Series* series : entries) {
        cairo_text_extents_t extents{};
        cairo_text_extents(context, series->label.c_str(), &extents);
        text_width = std::max(text_width, extents.width);
    }
    const double width = std::min(plot.width - 8.0, text_width + 35.0);
    const double height = 8.0 + static_cast<double>(entries.size()) * 14.0;
    const double left = plot.x + plot.width - width - 5.0;
    const double top = plot.y + 5.0;

    cairo_set_source_rgba(context, 1.0, 1.0, 1.0, 0.9);
    cairo_rectangle(context, left, top, width, height);
    cairo_fill_preserve(context);
    cairo_set_source_rgb(context, 0.74, 0.75, 0.77);
    cairo_set_line_width(context, 0.5);
    cairo_stroke(context);

    for (std::size_t index = 0; index < entries.size(); ++index) {
        const Series& series = *entries[index];
        const double y = top + 11.0 + static_cast<double>(index) * 14.0;
        set_source(context, series.color);
        if (series.style == SeriesStyle::Bars) {
            cairo_rectangle(context, left + 7.0, y - 7.0, 14.0, 7.0);
            cairo_fill(context);
        } else if (series.style == SeriesStyle::Scatter) {
            cairo_arc(context, left + 14.0, y - 3.0, 2.0, 0.0,
                      2.0 * kPi);
            cairo_fill(context);
        } else {
            cairo_set_line_width(context, std::max(1.0, series.line_width));
            cairo_move_to(context, left + 7.0, y - 3.0);
            cairo_line_to(context, left + 21.0, y - 3.0);
            cairo_stroke(context);
        }
        cairo_set_source_rgb(context, 0.15, 0.17, 0.20);
        draw_text(context, series.label, left + 27.0, y, TextAlign::Left);
    }
}

void draw_plot_panel(cairo_t* context, const Rect& panel_rect,
                     const Panel& panel)
{
    cairo_set_source_rgb(context, 0.12, 0.14, 0.17);
    draw_fitted_text(context, panel.title,
                     panel_rect.x + panel_rect.width / 2.0,
                     panel_rect.y + 20.0, panel_rect.width - 20.0, 11.5, true,
                     TextAlign::Center);

    Rect plot{panel_rect.x + 57.0, panel_rect.y + 36.0,
              panel_rect.width - 76.0, panel_rect.height - 86.0};
    if (panel.square_axes) {
        const double side = std::min(plot.width, plot.height);
        plot.x += (plot.width - side) / 2.0;
        plot.y += (plot.height - side) / 2.0;
        plot.width = side;
        plot.height = side;
    }

    const Bounds bounds = find_bounds(panel);
    draw_plot_axes(context, plot, bounds, panel);

    cairo_save(context);
    cairo_rectangle(context, plot.x, plot.y, plot.width, plot.height);
    cairo_clip(context);
    for (const Series& series : panel.series) {
        draw_series(context, plot, bounds, series);
    }
    // Reference lines and their labels must remain legible over opaque bars.
    draw_reference_lines(context, plot, bounds, panel);
    cairo_restore(context);
    draw_legend(context, plot, panel);
}

void draw_image_panel(cairo_t* context, const Rect& panel_rect,
                      const Panel& panel,
                      const std::filesystem::path& output_path)
{
    const Image& image = *panel.image;
    cairo_set_source_rgb(context, 0.12, 0.14, 0.17);
    draw_fitted_text(context, panel.title,
                     panel_rect.x + panel_rect.width / 2.0,
                     panel_rect.y + 20.0, panel_rect.width - 20.0, 11.5, true,
                     TextAlign::Center);

    const int width = static_cast<int>(image.width());
    const int height = static_cast<int>(image.height());
    const int stride = cairo_format_stride_for_width(CAIRO_FORMAT_RGB24, width);
    if (stride < 0) {
        throw std::runtime_error("PDF '" + output_path.string() +
                                 "': Cairo rejected the image width");
    }
    const std::size_t stride_size = static_cast<std::size_t>(stride);
    if (image.height() >
        std::numeric_limits<std::size_t>::max() / stride_size) {
        throw std::length_error("PDF '" + output_path.string() +
                                "': image surface size overflows size_t");
    }
    std::vector<unsigned char> surface_bytes(stride_size * image.height(), 0);
    for (std::size_t y = 0; y < image.height(); ++y) {
        unsigned char* const row = surface_bytes.data() + y * stride_size;
        for (std::size_t x = 0; x < image.width(); ++x) {
            const std::uint32_t gray = image.pixels()[y * image.width() + x];
            const std::uint32_t pixel = gray | (gray << 8U) | (gray << 16U);
            std::memcpy(row + x * sizeof(pixel), &pixel, sizeof(pixel));
        }
    }

    Surface image_surface(
        cairo_image_surface_create_for_data(surface_bytes.data(),
                                            CAIRO_FORMAT_RGB24, width, height,
                                            stride),
        &cairo_surface_destroy);
    check_surface(image_surface.get(), output_path,
                  "creating embedded grayscale image");
    cairo_surface_mark_dirty(image_surface.get());

    const Rect available{panel_rect.x + 18.0, panel_rect.y + 34.0,
                         panel_rect.width - 64.0,
                         panel_rect.height - 67.0};
    const double scale =
        std::min(available.width / static_cast<double>(image.width()),
                 available.height / static_cast<double>(image.height()));
    const double target_width = static_cast<double>(image.width()) * scale;
    const double target_height = static_cast<double>(image.height()) * scale;
    const double image_x = available.x + (available.width - target_width) / 2.0;
    const double image_y = available.y + (available.height - target_height) / 2.0;

    cairo_save(context);
    cairo_translate(context, image_x, image_y);
    cairo_scale(context, scale, scale);
    cairo_set_source_surface(context, image_surface.get(), 0.0, 0.0);
    cairo_pattern_set_filter(cairo_get_source(context), CAIRO_FILTER_NEAREST);
    cairo_rectangle(context, 0.0, 0.0, static_cast<double>(image.width()),
                    static_cast<double>(image.height()));
    cairo_fill(context);
    cairo_restore(context);

    cairo_set_source_rgb(context, 0.18, 0.20, 0.22);
    cairo_set_line_width(context, 0.7);
    cairo_rectangle(context, image_x, image_y, target_width, target_height);
    cairo_stroke(context);

    const double bar_x = panel_rect.x + panel_rect.width - 34.0;
    const double bar_y = available.y + 5.0;
    const double bar_height = std::max(50.0, available.height - 10.0);
    Pattern gradient(cairo_pattern_create_linear(0.0, bar_y + bar_height,
                                                 0.0, bar_y),
                     &cairo_pattern_destroy);
    const cairo_status_t pattern_status = cairo_pattern_status(gradient.get());
    if (pattern_status != CAIRO_STATUS_SUCCESS) {
        throw_cairo_error(output_path, "creating grayscale colour bar",
                          pattern_status);
    }
    cairo_pattern_add_color_stop_rgb(gradient.get(), 0.0, 0.0, 0.0, 0.0);
    cairo_pattern_add_color_stop_rgb(gradient.get(), 1.0, 1.0, 1.0, 1.0);
    cairo_rectangle(context, bar_x, bar_y, 9.0, bar_height);
    cairo_set_source(context, gradient.get());
    cairo_fill_preserve(context);
    cairo_set_source_rgb(context, 0.25, 0.27, 0.30);
    cairo_set_line_width(context, 0.5);
    cairo_stroke(context);
    select_font(context, 7.0, false);
    draw_text(context, "255", bar_x + 13.0, bar_y + 6.0, TextAlign::Left);
    draw_text(context, "0", bar_x + 13.0, bar_y + bar_height,
              TextAlign::Left);

    const std::string dimensions = std::to_string(image.width()) + " x " +
                                   std::to_string(image.height()) + " pixels";
    cairo_set_source_rgb(context, 0.35, 0.37, 0.40);
    select_font(context, 7.5, false);
    draw_text(context, dimensions, panel_rect.x + panel_rect.width / 2.0,
              panel_rect.y + panel_rect.height - 8.0, TextAlign::Center);
}

void draw_panel_frame(cairo_t* context, const Rect& panel_rect)
{
    cairo_set_source_rgb(context, 1.0, 1.0, 1.0);
    cairo_rectangle(context, panel_rect.x, panel_rect.y, panel_rect.width,
                    panel_rect.height);
    cairo_fill_preserve(context);
    cairo_set_source_rgb(context, 0.82, 0.83, 0.85);
    cairo_set_line_width(context, 0.6);
    cairo_stroke(context);
}

}  // namespace

void write_plot_grid_pdf(const std::filesystem::path& path,
                         std::string_view title,
                         std::string_view subtitle,
                         const std::vector<Panel>& panels,
                         std::size_t columns)
{
    if (path.empty()) {
        throw std::invalid_argument("PDF output path must not be empty");
    }
    if (panels.empty()) {
        throw std::invalid_argument("PDF plot grid must contain at least one panel");
    }
    if (columns == 0) {
        throw std::invalid_argument("PDF plot grid column count must be positive");
    }
    for (std::size_t index = 0; index < panels.size(); ++index) {
        validate_panel(panels[index], index);
    }

    const std::size_t actual_columns = std::min(columns, panels.size());
    const std::size_t rows = 1 + (panels.size() - 1) / actual_columns;
    const double column_count = static_cast<double>(actual_columns);
    const double row_count = static_cast<double>(rows);
    const double page_width =
        2.0 * kPageMargin + column_count * kPanelWidth +
        static_cast<double>(actual_columns - 1) * kPanelGap;
    const double header_height = subtitle.empty() ? 48.0 : 64.0;
    const double page_height =
        kPageMargin + header_height + row_count * kPanelHeight +
        static_cast<double>(rows - 1) * kPanelGap + kPageMargin;
    if (!finite(page_width) || !finite(page_height) ||
        page_width > kMaximumPageDimension ||
        page_height > kMaximumPageDimension) {
        throw std::length_error(
            "PDF plot grid exceeds the supported 200-inch page dimension");
    }

    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::error_code error;
        std::filesystem::create_directories(parent, error);
        if (error) {
            throw std::runtime_error("PDF '" + path.string() +
                                     "': cannot create parent directory: " +
                                     error.message());
        }
    }

    const std::string path_string = path.string();
    Surface surface(cairo_pdf_surface_create(path_string.c_str(), page_width,
                                             page_height),
                    &cairo_surface_destroy);
    check_surface(surface.get(), path, "creating Cairo PDF surface");
    cairo_surface_set_fallback_resolution(surface.get(), 144.0, 144.0);

#if CAIRO_VERSION >= CAIRO_VERSION_ENCODE(1, 16, 0)
    const std::string document_title(title);
    cairo_pdf_surface_set_metadata(surface.get(), CAIRO_PDF_METADATA_TITLE,
                                   document_title.c_str());
    cairo_pdf_surface_set_metadata(surface.get(), CAIRO_PDF_METADATA_CREATOR,
                                   "xormap_image C++");
#endif

    Context context(cairo_create(surface.get()), &cairo_destroy);
    check_context(context.get(), path, "creating Cairo drawing context");
    cairo_set_source_rgb(context.get(), 0.97, 0.975, 0.98);
    cairo_paint(context.get());

    cairo_set_source_rgb(context.get(), 0.10, 0.12, 0.15);
    draw_fitted_text(context.get(), std::string(title), page_width / 2.0,
                     kPageMargin + 4.0, page_width - 2.0 * kPageMargin, 18.0,
                     true, TextAlign::Center);
    if (!subtitle.empty()) {
        cairo_set_source_rgb(context.get(), 0.35, 0.37, 0.41);
        draw_fitted_text(context.get(), std::string(subtitle),
                         page_width / 2.0, kPageMargin + 24.0,
                         page_width - 2.0 * kPageMargin, 9.5, false,
                         TextAlign::Center);
    }

    const double grid_top = kPageMargin + header_height;
    for (std::size_t index = 0; index < panels.size(); ++index) {
        const std::size_t column = index % actual_columns;
        const std::size_t row = index / actual_columns;
        const Rect panel_rect{
            kPageMargin + static_cast<double>(column) *
                              (kPanelWidth + kPanelGap),
            grid_top + static_cast<double>(row) *
                           (kPanelHeight + kPanelGap),
            kPanelWidth,
            kPanelHeight};
        draw_panel_frame(context.get(), panel_rect);
        if (panels[index].kind == PanelKind::GrayscaleImage) {
            draw_image_panel(context.get(), panel_rect, panels[index], path);
        } else {
            draw_plot_panel(context.get(), panel_rect, panels[index]);
        }
        check_context(context.get(), path,
                      "rendering panel " + std::to_string(index + 1));
    }

    cairo_show_page(context.get());
    check_context(context.get(), path, "finishing PDF page");
    cairo_surface_finish(surface.get());
    check_surface(surface.get(), path, "writing PDF output");
}

Panel make_grayscale_image_panel(std::string title, const Image& image)
{
    image.validate();
    Panel panel;
    panel.kind = PanelKind::GrayscaleImage;
    panel.title = std::move(title);
    panel.show_grid = false;
    panel.show_legend = false;
    panel.image = image;
    return panel;
}

Panel make_histogram_panel(std::string title,
                           const std::vector<std::uint8_t>& bytes,
                           Color color)
{
    if (bytes.empty()) {
        throw std::invalid_argument("cannot plot a histogram of no bytes");
    }
    validate_color(color, "histogram");
    std::array<std::size_t, 256> counts{};
    for (const std::uint8_t value : bytes) {
        ++counts[value];
    }

    Series histogram;
    histogram.label = "observed";
    histogram.color = color;
    histogram.style = SeriesStyle::Bars;
    histogram.line_width = 1.0;
    histogram.points.reserve(counts.size());
    for (std::size_t value = 0; value < counts.size(); ++value) {
        histogram.points.push_back(
            {static_cast<double>(value), static_cast<double>(counts[value])});
    }

    Panel panel;
    panel.title = std::move(title);
    panel.x_label = "Pixel value";
    panel.y_label = "Count";
    panel.series.push_back(std::move(histogram));
    panel.x_range = AxisRange{0.0, 255.0};
    panel.show_legend = false;
    panel.reference_lines.push_back(
        {static_cast<double>(bytes.size()) / 256.0,
         "flat",
         Color{0.25, 0.27, 0.30, 0.75},
         ReferenceOrientation::Horizontal,
         0.9});
    return panel;
}

Panel make_adjacent_scatter_panel(std::string title,
                                  const AdjacentPixelPairs& pairs,
                                  Color color)
{
    if (pairs.x.size() != pairs.y.size()) {
        throw std::invalid_argument(
            "adjacent-pair x and y vectors must have equal lengths");
    }
    if (pairs.x.empty()) {
        throw std::invalid_argument("cannot plot an empty adjacent-pair sample");
    }
    validate_color(color, "adjacent-pair scatter");

    Series sample;
    sample.label = "sampled pairs";
    sample.color = color;
    sample.style = SeriesStyle::Scatter;
    sample.line_width = 1.0;
    sample.marker_radius = 1.15;
    sample.points.reserve(pairs.x.size());
    for (std::size_t index = 0; index < pairs.x.size(); ++index) {
        sample.points.push_back({static_cast<double>(pairs.x[index]),
                                 static_cast<double>(pairs.y[index])});
    }

    Panel panel;
    panel.title = std::move(title);
    panel.x_label = "Pixel value";
    panel.y_label = "Adjacent value";
    panel.series.push_back(std::move(sample));
    panel.x_range = AxisRange{0.0, 255.0};
    panel.y_range = AxisRange{0.0, 255.0};
    panel.show_legend = false;
    panel.square_axes = true;
    return panel;
}

void write_image_comparison_pdf(const std::filesystem::path& path,
                                std::string_view title,
                                const Image& plain,
                                const Image& cipher,
                                std::string_view subtitle)
{
    std::vector<Panel> panels;
    panels.reserve(2);
    panels.push_back(make_grayscale_image_panel("Plain", plain));
    panels.push_back(make_grayscale_image_panel("Cipher", cipher));
    write_plot_grid_pdf(path, title, subtitle, panels, 2);
}

void write_histogram_comparison_pdf(
    const std::filesystem::path& path,
    std::string_view title,
    const std::vector<std::uint8_t>& plain,
    const std::vector<std::uint8_t>& cipher,
    std::string_view subtitle)
{
    std::vector<Panel> panels;
    panels.reserve(2);
    panels.push_back(make_histogram_panel(
        "Plain histogram", plain, Color{0.922, 0.408, 0.204, 0.88}));
    panels.push_back(make_histogram_panel(
        "Cipher histogram", cipher, Color{0.165, 0.471, 0.839, 0.88}));
    // MATLAB run_all.m plots only the observed bars. The reusable histogram
    // panel retains its uniform reference for analysis_gray.m, where MATLAB
    // explicitly draws the corresponding `flat` line.
    for (Panel& panel : panels) {
        panel.reference_lines.clear();
    }
    write_plot_grid_pdf(path, title, subtitle, panels, 2);
}

void write_histogram_comparison_pdf(const std::filesystem::path& path,
                                    std::string_view title,
                                    const Image& plain,
                                    const Image& cipher,
                                    std::string_view subtitle)
{
    plain.validate();
    cipher.validate();
    write_histogram_comparison_pdf(path, title, plain.pixels(),
                                   cipher.pixels(), subtitle);
}

void write_adjacent_scatter_comparison_pdf(
    const std::filesystem::path& path,
    std::string_view title,
    const AdjacentPixelPairs& plain,
    const AdjacentPixelPairs& cipher,
    std::string_view subtitle)
{
    std::vector<Panel> panels;
    panels.reserve(2);
    panels.push_back(make_adjacent_scatter_panel(
        "Plain (horizontal)", plain, Color{0.922, 0.408, 0.204, 0.40}));
    panels.push_back(make_adjacent_scatter_panel(
        "Cipher (horizontal)", cipher, Color{0.165, 0.471, 0.839, 0.40}));
    write_plot_grid_pdf(path, title, subtitle, panels, 2);
}

}  // namespace xormap_image
