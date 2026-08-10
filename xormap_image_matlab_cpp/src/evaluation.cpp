#include "xormap_image/evaluation.hpp"

#include "xormap_image/core.hpp"
#include "xormap_image/download.hpp"
#include "xormap_image/image.hpp"
#include "xormap_image/metrics.hpp"
#include "xormap_image/parallel.hpp"
#include "xormap_image/plot.hpp"
#include "xormap_image/threefry.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace xormap_image {
namespace {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

constexpr std::size_t kBitsPerByte = 8U;

struct ThreeImageSpec {
    const char* filename;
    const char* short_name;
    const char* display_name;
};

constexpr std::array<ThreeImageSpec, 3> kThreeImages{{
    {"boat.512.tiff", "boat.512", "boat.512 (Fishing Boat, 512x512)"},
    {"5.1.09.tiff", "5.1.09", "5.1.09 (Moon Surface, 256x256)"},
    {"7.1.01.tiff", "7.1.01", "7.1.01 (Truck, 512x512)"},
}};

[[nodiscard]] std::ofstream open_output(const fs::path& path)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("could not open output file '" + path.string() + "'");
    }
    return output;
}

void finish_output(std::ofstream& output, const fs::path& path)
{
    output.flush();
    if (!output) {
        throw std::runtime_error("failed while writing output file '" + path.string() + "'");
    }
}

void make_directory(const fs::path& path)
{
    if (path.empty()) {
        throw std::invalid_argument("output directory must not be empty");
    }
    std::error_code error;
    fs::create_directories(path, error);
    if (error) {
        throw std::runtime_error("could not create directory '" + path.string() +
                                 "': " + error.message());
    }
}

[[nodiscard]] std::size_t expected_iterations(std::size_t num_bytes,
                                               std::size_t k)
{
    if (k <= 4U) {
        throw std::invalid_argument("K must be greater than 4");
    }
    if (num_bytes > std::numeric_limits<std::size_t>::max() / kBitsPerByte) {
        throw std::length_error("image is too large to express its bit count");
    }
    const std::size_t bits = num_bytes * kBitsPerByte;
    return bits / k + (bits % k == 0U ? 0U : 1U);
}

void validate_k_values(const std::vector<std::size_t>& values)
{
    if (values.empty()) {
        throw std::invalid_argument("at least one K value is required");
    }
    for (const std::size_t k : values) {
        if (k <= 4U) {
            throw std::invalid_argument("every K value must be greater than 4");
        }
    }
}

void discard_adjacent_calls(MatlabTwister& generator,
                            std::size_t call_count,
                            std::size_t samples_per_call)
{
    // adjacent_correlation.m calls randi once for every row index, then once
    // for every column index. Each randi consumes one MATLAB rand value.
    for (std::size_t call = 0U; call < call_count; ++call) {
        generator.discard_uniform(samples_per_call);
        generator.discard_uniform(samples_per_call);
    }
}

[[nodiscard]] std::string fixed_number(double value, int precision,
                                       bool show_positive = false)
{
    if (std::isnan(value)) {
        return "NaN";
    }
    if (std::isinf(value)) {
        return value > 0.0 ? "Inf" : "-Inf";
    }
    std::ostringstream output;
    if (show_positive) {
        output << std::showpos;
    }
    output << std::fixed << std::setprecision(precision) << value;
    return output.str();
}

[[nodiscard]] std::array<std::size_t, 256> histogram(const Bytes& pixels)
{
    std::array<std::size_t, 256> counts{};
    for (const Byte pixel : pixels) {
        ++counts[pixel];
    }
    return counts;
}

[[nodiscard]] Bytes xor_bytes(const Bytes& first, const Bytes& second)
{
    if (first.size() != second.size()) {
        throw std::invalid_argument("XOR operands must have equal lengths");
    }
    Bytes result(first.size());
    for (std::size_t index = 0; index < first.size(); ++index) {
        result[index] = static_cast<Byte>(first[index] ^ second[index]);
    }
    return result;
}

void perturb_center_lsb(Bytes& pixels, std::size_t width, std::size_t height)
{
    if (width == 0U || height == 0U ||
        width > std::numeric_limits<std::size_t>::max() / height ||
        pixels.size() != width * height) {
        throw std::invalid_argument("cannot perturb an invalid image shape");
    }
    // MATLAB: floor(size/2)+1 in one-based coordinates.
    const std::size_t row = height / 2U;
    const std::size_t column = width / 2U;
    pixels[row * width + column] ^= Byte{1};
}

class ProgressCounter {
public:
    ProgressCounter(std::ostream& output, std::size_t total, std::string label)
        : output_(output),
          total_(total),
          stride_(std::max<std::size_t>(1U, total / 20U)),
          label_(std::move(label))
    {
    }

    void completed_one()
    {
        const std::size_t value = completed_.fetch_add(1U) + 1U;
        if (value != total_ && value % stride_ != 0U) {
            return;
        }
        std::lock_guard<std::mutex> lock(output_mutex_);
        output_ << label_ << ": " << value << '/' << total_ << " complete\n";
        output_.flush();
    }

private:
    std::ostream& output_;
    std::size_t total_;
    std::size_t stride_;
    std::string label_;
    std::atomic<std::size_t> completed_{0U};
    std::mutex output_mutex_;
};

[[nodiscard]] std::vector<Image> load_manifest_images(
    const std::vector<ManifestEntry>& entries,
    std::size_t workers,
    std::ostream& progress)
{
    std::vector<Image> images(entries.size());
    ProgressCounter counter(progress, entries.size(), "TIFF validation");
    parallel_for(entries.size(), workers, [&](std::size_t index) {
        Image image = load_tiff_gray8(entries[index].path);
        if (image.width() != entries[index].width ||
            image.height() != entries[index].height) {
            throw std::runtime_error(
                "manifest dimensions do not match TIFF '" +
                entries[index].path.string() + "'");
        }
        images[index] = std::move(image);
        counter.completed_one();
    });
    return images;
}

struct RunAllRow {
    std::string display_name;
    double entropy_plain = 0.0;
    double entropy_cipher = 0.0;
    double corr_h_plain = 0.0;
    double corr_h_cipher = 0.0;
    double corr_v_plain = 0.0;
    double corr_v_cipher = 0.0;
    double corr_d_plain = 0.0;
    double corr_d_cipher = 0.0;
    double npcr = 0.0;
    double uaci = 0.0;
};

void write_run_histogram(const fs::path& path,
                         const Bytes& plain,
                         const Bytes& cipher)
{
    const auto plain_counts = histogram(plain);
    const auto cipher_counts = histogram(cipher);
    std::ofstream output = open_output(path);
    output << "gray_level,plain_count,cipher_count\n";
    for (std::size_t value = 0; value < plain_counts.size(); ++value) {
        output << value << ',' << plain_counts[value] << ','
               << cipher_counts[value] << '\n';
    }
    finish_output(output, path);
}

void write_run_scatter(const fs::path& path,
                       const AdjacentPixelPairs& plain,
                       const AdjacentPixelPairs& cipher)
{
    if (plain.x.size() != plain.y.size() ||
        cipher.x.size() != cipher.y.size() ||
        plain.x.size() != cipher.x.size()) {
        throw std::logic_error("adjacent-pixel pair collection size mismatch");
    }
    std::ofstream output = open_output(path);
    output << "sample,plain_x,plain_y,cipher_x,cipher_y\n";
    for (std::size_t index = 0; index < plain.x.size(); ++index) {
        output << index + 1U << ',' << static_cast<unsigned int>(plain.x[index])
               << ',' << static_cast<unsigned int>(plain.y[index]) << ','
               << static_cast<unsigned int>(cipher.x[index]) << ','
               << static_cast<unsigned int>(cipher.y[index]) << '\n';
    }
    finish_output(output, path);
}

struct SweepThreeRow {
    std::size_t image_index = 0U;
    std::size_t k = 0U;
    double entropy = 0.0;
    double corr_h = 0.0;
    double corr_v = 0.0;
    double corr_d = 0.0;
    double npcr = 0.0;
    double uaci = 0.0;
    double seconds = 0.0;
};

struct SweepAllRow {
    std::size_t image_index = 0U;
    std::size_t k = 0U;
    std::size_t iterations = 0U;
    double entropy = 0.0;
    double abs_corr_h = 0.0;
    double npcr = 0.0;
    double uaci = 0.0;
    // Kept only for sweep_all_images()'s standalone CSV, whose header is
    // asserted verbatim by tests. run_tests()'s combined CSV omits it.
    double seconds = 0.0;
};

struct AnalysisRow {
    std::size_t image_index = 0U;
    std::size_t k = 0U;
    std::size_t num_pixels = 0U;
    double npcr_key = 0.0;
    double uaci_key = 0.0;
    double psnr_cipher_pair = 0.0;
    double psnr_plain_wrong_key = 0.0;
    double chi2_plain = 0.0;
    double chi2_cipher = 0.0;
    double chi2_critical = 0.0;
    double psnr_plain_cipher = 0.0;
    double psnr_roundtrip = 0.0;
    std::uint32_t checksum = 0U;
    std::size_t first_difference = 0U;
};

struct BitStudyRow {
    std::size_t k = 0U;
    std::size_t bit = 0U;
    double npcr = 0.0;
    double uaci = 0.0;
    double psnr = 0.0;
};

[[nodiscard]] std::vector<std::size_t> spread_bit_positions(std::size_t k)
{
    const std::size_t count = std::min<std::size_t>(k, 24U);
    std::set<std::size_t> positions;
    if (count == 1U) {
        positions.insert(1U);
    } else {
        for (std::size_t index = 0; index < count; ++index) {
            const long double value =
                1.0L + static_cast<long double>(index) *
                           static_cast<long double>(k - 1U) /
                           static_cast<long double>(count - 1U);
            positions.insert(static_cast<std::size_t>(std::floor(value + 0.5L)));
        }
    }
    return {positions.begin(), positions.end()};
}

struct CsvRecord {
    std::vector<std::string> fields;
    std::size_t line = 0U;
};

[[noreturn]] void csv_error(const fs::path& path,
                            std::size_t line,
                            const std::string& message)
{
    throw std::runtime_error("CSV '" + path.string() + "', line " +
                             std::to_string(line) + ": " + message);
}

[[nodiscard]] std::vector<CsvRecord> parse_csv(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("could not open sweep CSV '" + path.string() + "'");
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (input.bad()) {
        throw std::runtime_error("failed while reading sweep CSV '" + path.string() + "'");
    }
    const std::string text = buffer.str();

    enum class State { FieldStart, Unquoted, Quoted, QuoteClosed };
    State state = State::FieldStart;
    std::vector<CsvRecord> records;
    std::vector<std::string> fields;
    std::string field;
    std::size_t line = 1U;
    std::size_t record_line = 1U;
    bool started = false;

    const auto finish_field = [&]() {
        fields.push_back(std::move(field));
        field.clear();
        state = State::FieldStart;
    };
    const auto finish_record = [&]() {
        finish_field();
        records.push_back({std::move(fields), record_line});
        fields.clear();
        started = false;
    };

    std::size_t index = 0U;
    while (index < text.size()) {
        const char value = text[index];
        if (state == State::Quoted) {
            if (value == '"') {
                if (index + 1U < text.size() && text[index + 1U] == '"') {
                    field.push_back('"');
                    index += 2U;
                } else {
                    state = State::QuoteClosed;
                    ++index;
                }
                continue;
            }
            if (value == '\r' || value == '\n') {
                if (value == '\r' && index + 1U < text.size() &&
                    text[index + 1U] == '\n') {
                    ++index;
                }
                field.push_back('\n');
                ++line;
                ++index;
                continue;
            }
            field.push_back(value);
            ++index;
            continue;
        }

        if (state == State::QuoteClosed) {
            if (value == ',') {
                started = true;
                finish_field();
                ++index;
                continue;
            }
            if (value != '\r' && value != '\n') {
                csv_error(path, line, "unexpected character after closing quote");
            }
        } else if (value == ',') {
            started = true;
            finish_field();
            ++index;
            continue;
        } else if (value == '"') {
            if (state != State::FieldStart) {
                csv_error(path, line, "quote in unquoted field");
            }
            started = true;
            state = State::Quoted;
            ++index;
            continue;
        } else if (value != '\r' && value != '\n') {
            started = true;
            state = State::Unquoted;
            field.push_back(value);
            ++index;
            continue;
        }

        finish_record();
        if (value == '\r' && index + 1U < text.size() &&
            text[index + 1U] == '\n') {
            ++index;
        }
        ++line;
        ++index;
        record_line = line;
    }
    if (state == State::Quoted) {
        csv_error(path, record_line, "unterminated quoted field");
    }
    if (started || !fields.empty() || !field.empty() ||
        state == State::QuoteClosed) {
        finish_record();
    }

    // A blank line ends the table: run-tests/sweep-all follow it with a
    // second table (a different schema, e.g. the per-bit key-sensitivity
    // study) in the same file. Truncate there instead of skipping past it,
    // so that appendix is never parsed as more rows of this table.
    const auto is_blank = [](const CsvRecord& record) {
        return record.fields.size() == 1U && record.fields.front().empty();
    };
    records.erase(std::find_if(records.begin(), records.end(), is_blank),
                  records.end());
    if (records.empty()) {
        csv_error(path, 1U, "file is empty");
    }
    if (!records.front().fields.empty() &&
        records.front().fields.front().size() >= 3U) {
        std::string& first = records.front().fields.front();
        const auto byte = [](char character) {
            return static_cast<unsigned char>(character);
        };
        if (byte(first[0]) == 0xEFU && byte(first[1]) == 0xBBU &&
            byte(first[2]) == 0xBFU) {
            first.erase(0U, 3U);
        }
    }
    return records;
}

[[nodiscard]] std::string_view trim(std::string_view value)
{
    constexpr std::string_view whitespace = " \t\r\n";
    const std::size_t first = value.find_first_not_of(whitespace);
    if (first == std::string_view::npos) {
        return {};
    }
    const std::size_t last = value.find_last_not_of(whitespace);
    return value.substr(first, last - first + 1U);
}

[[nodiscard]] std::size_t parse_size(std::string_view value,
                                     const fs::path& path,
                                     std::size_t line,
                                     const std::string& column)
{
    value = trim(value);
    if (value.empty() || value.front() == '-') {
        csv_error(path, line, "column '" + column + "' is not an unsigned integer");
    }
    const std::string copy(value);
    std::size_t consumed = 0U;
    unsigned long long parsed = 0ULL;
    try {
        parsed = std::stoull(copy, &consumed, 10);
    } catch (const std::exception&) {
        csv_error(path, line, "column '" + column + "' is not an unsigned integer");
    }
    if (consumed != copy.size() ||
        parsed > static_cast<unsigned long long>(
                     std::numeric_limits<std::size_t>::max())) {
        csv_error(path, line, "column '" + column + "' is out of range");
    }
    return static_cast<std::size_t>(parsed);
}

[[nodiscard]] double parse_double(std::string_view value,
                                  const fs::path& path,
                                  std::size_t line,
                                  const std::string& column)
{
    value = trim(value);
    const std::string copy(value);
    std::size_t consumed = 0U;
    double parsed = 0.0;
    try {
        parsed = std::stod(copy, &consumed);
    } catch (const std::exception&) {
        csv_error(path, line, "column '" + column + "' is not numeric");
    }
    if (copy.empty() || consumed != copy.size()) {
        csv_error(path, line, "column '" + column + "' is not numeric");
    }
    return parsed;
}

[[nodiscard]] std::map<std::string, std::size_t> header_map(
    const CsvRecord& header,
    const fs::path& path)
{
    std::map<std::string, std::size_t> columns;
    for (std::size_t index = 0U; index < header.fields.size(); ++index) {
        if (header.fields[index].empty()) {
            csv_error(path, header.line, "header contains an empty column name");
        }
        if (!columns.emplace(header.fields[index], index).second) {
            csv_error(path, header.line,
                      "duplicate column '" + header.fields[index] + "'");
        }
    }
    return columns;
}

[[nodiscard]] std::size_t require_column(
    const std::map<std::string, std::size_t>& columns,
    const std::string& name,
    const fs::path& path,
    std::size_t line)
{
    const auto found = columns.find(name);
    if (found == columns.end()) {
        csv_error(path, line, "missing required column '" + name + "'");
    }
    return found->second;
}

struct SweepMeans {
    std::size_t count = 0U;
    double entropy = 0.0;
    double corr_h = 0.0;
    double npcr = 0.0;
    double uaci = 0.0;
    double seconds = 0.0;
};

constexpr Color kBlue{0.165, 0.471, 0.839, 0.86};
constexpr Color kOrange{0.922, 0.408, 0.204, 0.86};
constexpr Color kGray{0.62, 0.62, 0.60, 0.24};
constexpr std::array<Color, 3> kImageColors{{
    kBlue,
    kOrange,
    Color{0.220, 0.650, 0.430, 0.86},
}};

[[nodiscard]] Series plot_series(std::string label,
                                 std::vector<Point> points,
                                 Color color,
                                 SeriesStyle style,
                                 double marker_radius)
{
    Series series;
    series.label = std::move(label);
    series.points = std::move(points);
    series.color = color;
    series.style = style;
    series.line_width = style == SeriesStyle::Line ? 1.8 : 0.7;
    series.marker_radius = marker_radius;
    return series;
}

void add_ideal_line(Panel& panel, double value, std::string label = "ideal")
{
    panel.reference_lines.push_back(
        {value, std::move(label), Color{0.25, 0.27, 0.30, 0.75},
         ReferenceOrientation::Horizontal, 0.9});
}

template <typename Row, typename Value>
[[nodiscard]] std::vector<Point> mean_points_for(const std::vector<Row>& rows,
                                                  Value value)
{
    std::map<std::size_t, std::pair<long double, std::size_t>> totals;
    for (const Row& row : rows) {
        const double measured = value(row);
        if (!std::isfinite(measured)) {
            continue;
        }
        auto& total = totals[row.k];
        total.first += measured;
        ++total.second;
    }
    std::vector<Point> means;
    means.reserve(totals.size());
    for (const auto& entry : totals) {
        means.push_back({
            static_cast<double>(entry.first),
            static_cast<double>(entry.second.first /
                                static_cast<long double>(entry.second.second)),
        });
    }
    return means;
}

// Plots NPCR and UACI as two mean-over-images lines in one panel instead of
// two separate panels, since both are percentages read against K.
template <typename Row, typename NpcrValue, typename UaciValue>
[[nodiscard]] Panel combined_npcr_uaci_panel(const std::vector<Row>& rows,
                                             NpcrValue npcr_value,
                                             double npcr_ideal,
                                             UaciValue uaci_value,
                                             double uaci_ideal,
                                             std::string title)
{
    Panel panel;
    panel.title = std::move(title);
    panel.x_label = "K (state bits)";
    panel.y_label = "Percent (%)";
    panel.series.push_back(plot_series("NPCR", mean_points_for(rows, npcr_value),
                                       kOrange, SeriesStyle::Line, 2.2));
    panel.series.push_back(plot_series("UACI", mean_points_for(rows, uaci_value),
                                       kBlue, SeriesStyle::Line, 2.2));
    if (std::isfinite(npcr_ideal)) {
        add_ideal_line(panel, npcr_ideal, "NPCR ideal");
    }
    if (std::isfinite(uaci_ideal)) {
        add_ideal_line(panel, uaci_ideal, "UACI ideal");
    }
    return panel;
}

template <typename Row, typename Value>
[[nodiscard]] Panel scatter_mean_panel(const std::vector<Row>& rows,
                                       Value value,
                                       std::string title,
                                       std::string y_label,
                                       Color mean_color,
                                       std::optional<double> ideal = std::nullopt,
                                       std::string point_label = "individual images")
{
    std::vector<Point> points;
    points.reserve(rows.size());
    std::map<std::size_t, std::pair<long double, std::size_t>> totals;
    for (const Row& row : rows) {
        const double measured = value(row);
        if (!std::isfinite(measured)) {
            continue;
        }
        points.push_back({static_cast<double>(row.k), measured});
        auto& total = totals[row.k];
        total.first += measured;
        ++total.second;
    }
    if (points.empty()) {
        throw std::runtime_error("cannot plot an entirely non-finite metric: " + title);
    }

    std::vector<Point> means;
    means.reserve(totals.size());
    for (const auto& entry : totals) {
        means.push_back({
            static_cast<double>(entry.first),
            static_cast<double>(entry.second.first /
                                static_cast<long double>(entry.second.second)),
        });
    }

    Panel panel;
    panel.title = std::move(title);
    panel.x_label = "K (state bits)";
    panel.y_label = std::move(y_label);
    panel.series.push_back(plot_series(std::move(point_label), std::move(points),
                                       kGray, SeriesStyle::Scatter, 0.75));
    panel.series.push_back(plot_series("mean", std::move(means), mean_color,
                                       SeriesStyle::Line, 2.2));
    if (ideal.has_value()) {
        add_ideal_line(panel, *ideal);
    }
    return panel;
}

template <typename Value>
[[nodiscard]] Panel three_image_panel(const std::vector<SweepThreeRow>& rows,
                                      Value value,
                                      std::string title,
                                      std::string y_label,
                                      std::optional<double> ideal = std::nullopt)
{
    Panel panel;
    panel.title = std::move(title);
    panel.x_label = "K (state bits)";
    panel.y_label = std::move(y_label);
    for (std::size_t image_index = 0U; image_index < kThreeImages.size();
         ++image_index) {
        std::vector<Point> points;
        for (const SweepThreeRow& row : rows) {
            if (row.image_index == image_index) {
                const double measured = value(row);
                if (std::isfinite(measured)) {
                    points.push_back({static_cast<double>(row.k), measured});
                }
            }
        }
        panel.series.push_back(plot_series(
            kThreeImages[image_index].short_name, std::move(points),
            kImageColors[image_index], SeriesStyle::Line, 2.0));
    }
    if (ideal.has_value()) {
        add_ideal_line(panel, *ideal);
    }
    return panel;
}

void share_y_range(Panel& first, Panel& second)
{
    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -std::numeric_limits<double>::infinity();
    const auto include = [&](const Panel& panel) {
        for (const Series& series : panel.series) {
            for (const Point& point : series.points) {
                minimum = std::min(minimum, point.y);
                maximum = std::max(maximum, point.y);
            }
        }
        for (const ReferenceLine& line : panel.reference_lines) {
            if (line.orientation == ReferenceOrientation::Horizontal) {
                minimum = std::min(minimum, line.value);
                maximum = std::max(maximum, line.value);
            }
        }
    };
    include(first);
    include(second);
    if (!std::isfinite(minimum) || !std::isfinite(maximum)) {
        throw std::logic_error("cannot share a non-finite plot range");
    }
    double span = maximum - minimum;
    if (span <= 0.0) {
        span = std::max(1.0e-6, std::abs(maximum) * 0.05);
    }
    const AxisRange range{minimum - span * 0.08, maximum + span * 0.08};
    first.y_range = range;
    second.y_range = range;
}

[[nodiscard]] std::vector<Point> distribution_histogram(
    const std::vector<double>& values,
    std::size_t bin_count)
{
    if (values.empty() || bin_count == 0U) {
        throw std::invalid_argument("distribution histogram needs values and bins");
    }
    const auto limits = std::minmax_element(values.begin(), values.end());
    double minimum = *limits.first;
    double maximum = *limits.second;
    if (minimum == maximum) {
        const double padding = std::max(0.5, std::abs(minimum) * 0.05);
        minimum -= padding;
        maximum += padding;
    }
    const double width =
        (maximum - minimum) / static_cast<double>(bin_count);
    std::vector<std::size_t> counts(bin_count, 0U);
    for (const double value : values) {
        std::size_t bin = static_cast<std::size_t>(
            std::floor((value - minimum) / width));
        if (bin >= bin_count) {
            bin = bin_count - 1U;
        }
        ++counts[bin];
    }
    std::vector<Point> points;
    points.reserve(bin_count);
    for (std::size_t bin = 0U; bin < bin_count; ++bin) {
        points.push_back({
            minimum + (static_cast<double>(bin) + 0.5) * width,
            static_cast<double>(counts[bin]),
        });
    }
    return points;
}

[[nodiscard]] Panel make_log_chi_square_distribution_panel(
    const std::vector<AnalysisRow>& rows,
    double critical)
{
    std::vector<double> plain;
    std::vector<double> cipher;
    plain.reserve(rows.size());
    cipher.reserve(rows.size());
    for (const AnalysisRow& row : rows) {
        plain.push_back(std::log10(std::max(row.chi2_plain, 1.0)));
        cipher.push_back(std::log10(std::max(row.chi2_cipher, 1.0)));
    }

    Panel panel;
    panel.title = "Plain vs cipher chi-square";
    panel.x_label = "log10 chi-square";
    panel.y_label = "Count";
    panel.series.push_back(plot_series(
        "plain", distribution_histogram(plain, 40U),
        Color{kOrange.red, kOrange.green, kOrange.blue, 0.68},
        SeriesStyle::Bars, 0.0));
    panel.series.push_back(plot_series(
        "cipher", distribution_histogram(cipher, 40U),
        Color{kBlue.red, kBlue.green, kBlue.blue, 0.68},
        SeriesStyle::Bars, 0.0));
    panel.reference_lines.push_back({
        std::log10(critical), "5% critical", Color{0.25, 0.27, 0.30, 0.75},
        ReferenceOrientation::Vertical, 0.9,
    });
    return panel;
}

[[nodiscard]] std::map<std::size_t, SweepMeans> means_by_k(
    const NormalizedSweep& sweep)
{
    std::map<std::size_t, SweepMeans> groups;
    for (const auto& row : sweep.rows) {
        SweepMeans& group = groups[row.k];
        ++group.count;
        group.entropy += row.entropy;
        group.corr_h += row.corr_h;
        group.npcr += row.npcr;
        group.uaci += row.uaci;
        group.seconds += row.seconds;
    }
    for (auto& entry : groups) {
        SweepMeans& group = entry.second;
        const double count = static_cast<double>(group.count);
        group.entropy /= count;
        group.corr_h /= count;
        group.npcr /= count;
        group.uaci /= count;
    }
    return groups;
}

}  // namespace

std::vector<std::size_t> make_k_values(std::size_t first,
                                       std::size_t step,
                                       std::size_t last)
{
    if (step == 0U) {
        throw std::invalid_argument("K step must be greater than zero");
    }
    if (first <= 4U) {
        throw std::invalid_argument("first K value must be greater than 4");
    }
    std::vector<std::size_t> values;
    if (first > last) {
        return values;
    }
    for (std::size_t value = first;;) {
        values.push_back(value);
        if (last - value < step) {
            break;
        }
        value += step;
    }
    return values;
}

void run_all(const RunAllOptions& options, std::ostream& progress)
{
    if (options.k <= 4U) {
        throw std::invalid_argument("K must be greater than 4");
    }
    if (options.correlation_samples == 0U || options.scatter_samples == 0U) {
        throw std::invalid_argument("correlation and scatter sample counts must be positive");
    }
    make_directory(options.paths.results_directory);

    std::array<Image, kThreeImages.size()> images;
    for (std::size_t index = 0U; index < kThreeImages.size(); ++index) {
        images[index] = load_tiff_gray8(options.paths.images_directory /
                                        kThreeImages[index].filename);
    }

    // run_all.m owns one global rng(2026) stream. Snapshot that stream at
    // each image boundary in serial MATLAB order; workers receive private
    // copies, retaining parallel encryption and artifact generation without
    // racing or changing any sampled coordinates.
    MatlabTwister sampling_stream(2026U);
    std::vector<MatlabTwister> sampling_states;
    sampling_states.reserve(kThreeImages.size());
    for (std::size_t index = 0U; index < kThreeImages.size(); ++index) {
        sampling_states.push_back(sampling_stream);
        discard_adjacent_calls(sampling_stream, 6U,
                               options.correlation_samples);
        discard_adjacent_calls(sampling_stream, 2U, options.scatter_samples);
    }

    const Bits key = secret_key(options.k);
    std::array<RunAllRow, kThreeImages.size()> rows;
    ProgressCounter counter(progress, rows.size(), "run-all images");
    parallel_for(rows.size(), options.workers, [&](std::size_t index) {
        const Image& plain_image = images[index];
        const Bytes& plain = plain_image.pixels();
        Bytes perturbed = plain;
        perturb_center_lsb(perturbed, plain_image.width(), plain_image.height());

        const EncryptionResult encrypted = encrypt_fast(plain, key);
        const EncryptionResult encrypted_perturbed = encrypt_fast(perturbed, key);
        const std::size_t iterations = expected_iterations(plain.size(), options.k);
        if (encrypted.iterations != iterations ||
            encrypted_perturbed.iterations != iterations) {
            throw std::logic_error("iteration count mismatch in run-all");
        }
        const Bytes recovered = decrypt_fast(encrypted.cipher, encrypted.seed);
        if (recovered != plain) {
            throw std::runtime_error("round-trip failed for " +
                                     std::string(kThreeImages[index].display_name));
        }

        RunAllRow row;
        row.display_name = kThreeImages[index].display_name;
        row.entropy_plain = shannon_entropy(plain);
        row.entropy_cipher = shannon_entropy(encrypted.cipher);
        MatlabTwister sampling = sampling_states[index];
        // Preserve run_all.m's exact call order: all three plaintext
        // directions, then all three ciphertext directions, then scatters.
        row.corr_h_plain = adjacent_correlation(
            plain, plain_image.width(), plain_image.height(),
            AdjacentDirection::Horizontal, options.correlation_samples,
            sampling).correlation;
        row.corr_v_plain = adjacent_correlation(
            plain, plain_image.width(), plain_image.height(),
            AdjacentDirection::Vertical, options.correlation_samples,
            sampling).correlation;
        row.corr_d_plain = adjacent_correlation(
            plain, plain_image.width(), plain_image.height(),
            AdjacentDirection::Diagonal, options.correlation_samples,
            sampling).correlation;
        row.corr_h_cipher = adjacent_correlation(
            encrypted.cipher, plain_image.width(), plain_image.height(),
            AdjacentDirection::Horizontal, options.correlation_samples,
            sampling).correlation;
        row.corr_v_cipher = adjacent_correlation(
            encrypted.cipher, plain_image.width(), plain_image.height(),
            AdjacentDirection::Vertical, options.correlation_samples,
            sampling).correlation;
        row.corr_d_cipher = adjacent_correlation(
            encrypted.cipher, plain_image.width(), plain_image.height(),
            AdjacentDirection::Diagonal, options.correlation_samples,
            sampling).correlation;
        const NpcrUaciResult change = npcr_uaci(encrypted.cipher,
                                                encrypted_perturbed.cipher);
        row.npcr = change.npcr_percent;
        row.uaci = change.uaci_percent;

        const AdjacentCorrelationResult plain_scatter = adjacent_correlation(
            plain, plain_image.width(), plain_image.height(),
            AdjacentDirection::Horizontal, options.scatter_samples,
            sampling, true);
        const AdjacentCorrelationResult cipher_scatter = adjacent_correlation(
            encrypted.cipher, plain_image.width(), plain_image.height(),
            AdjacentDirection::Horizontal, options.scatter_samples,
            sampling, true);
        if (!plain_scatter.pairs || !cipher_scatter.pairs) {
            throw std::logic_error("scatter sampling did not return its pixel pairs");
        }

        const fs::path stem = options.paths.results_directory /
                              kThreeImages[index].short_name;
        const Image cipher_image(plain_image.width(), plain_image.height(),
                                 encrypted.cipher);
        const Image recovered_image(plain_image.width(), plain_image.height(),
                                    recovered);
        write_tiff_gray8(stem.string() + "_plain.tiff", plain_image);
        write_tiff_gray8(stem.string() + "_cipher.tiff", cipher_image);
        write_tiff_gray8(stem.string() + "_recovered.tiff", recovered_image);
        write_run_histogram(stem.string() + "_histogram.csv", plain,
                            encrypted.cipher);
        write_run_scatter(stem.string() + "_correlation.csv",
                          *plain_scatter.pairs, *cipher_scatter.pairs);
        const std::string subtitle = "K=" + std::to_string(options.k) +
                                     "; native C++ vector report";
        write_image_comparison_pdf(stem.string() + "_images.pdf", row.display_name,
                                   plain_image, cipher_image, subtitle);
        write_histogram_comparison_pdf(stem.string() + "_histogram.pdf",
                                       row.display_name, plain, encrypted.cipher,
                                       subtitle);
        write_adjacent_scatter_comparison_pdf(
            stem.string() + "_correlation.pdf", row.display_name,
            *plain_scatter.pairs, *cipher_scatter.pairs, subtitle);
        rows[index] = std::move(row);
        counter.completed_one();
    });

    const fs::path markdown_path = options.paths.results_directory / "results.md";
    std::ofstream markdown = open_output(markdown_path);
    markdown << "| Image | Entropy (plain) | Entropy (cipher) | Corr-H (plain) | "
                "Corr-H (cipher) | Corr-V (plain) | Corr-V (cipher) | "
                "Corr-D (plain) | Corr-D (cipher) | NPCR % | UACI % |\n";
    markdown << "|---|---|---|---|---|---|---|---|---|---|---|\n";
    for (const RunAllRow& row : rows) {
        markdown << "| " << row.display_name << " | "
                 << fixed_number(row.entropy_plain, 4) << " | "
                 << fixed_number(row.entropy_cipher, 4) << " | "
                 << fixed_number(row.corr_h_plain, 4, true) << " | "
                 << fixed_number(row.corr_h_cipher, 4, true) << " | "
                 << fixed_number(row.corr_v_plain, 4, true) << " | "
                 << fixed_number(row.corr_v_cipher, 4, true) << " | "
                 << fixed_number(row.corr_d_plain, 4, true) << " | "
                 << fixed_number(row.corr_d_cipher, 4, true) << " | "
                 << fixed_number(row.npcr, 4) << " | "
                 << fixed_number(row.uaci, 4) << " |\n";
    }
    finish_output(markdown, markdown_path);

    const NpcrUaciResult ideal = npcr_uaci_ideal(8);
    for (const RunAllRow& row : rows) {
        progress << "\n=== " << row.display_name << " ===\n"
                 << "Round-trip OK (decrypt(encrypt(P)) == P)\n"
                 << "Entropy        plain=" << fixed_number(row.entropy_plain, 4)
                 << "   cipher=" << fixed_number(row.entropy_cipher, 4)
                 << "   (ideal 8.0000)\n"
                 << "Correlation H  plain=" << fixed_number(row.corr_h_plain, 4, true)
                 << "  cipher=" << fixed_number(row.corr_h_cipher, 4, true) << '\n'
                 << "Correlation V  plain=" << fixed_number(row.corr_v_plain, 4, true)
                 << "  cipher=" << fixed_number(row.corr_v_cipher, 4, true) << '\n'
                 << "Correlation D  plain=" << fixed_number(row.corr_d_plain, 4, true)
                 << "  cipher=" << fixed_number(row.corr_d_cipher, 4, true) << '\n'
                 << "NPCR = " << fixed_number(row.npcr, 4) << "%  (ideal "
                 << fixed_number(ideal.npcr_percent, 4) << "%)   UACI = "
                 << fixed_number(row.uaci, 4) << "%  (ideal "
                 << fixed_number(ideal.uaci_percent, 4) << "%)\n";
    }
    progress << "\nWrote results table and TIFF/histogram/scatter artifacts to "
             << options.paths.results_directory << '\n';
}

void sweep_three_images(const SweepOptions& options, std::ostream& progress)
{
    validate_k_values(options.k_values);
    if (options.correlation_samples == 0U) {
        throw std::invalid_argument("correlation sample count must be positive");
    }
    make_directory(options.paths.results_directory);

    std::array<Image, kThreeImages.size()> images;
    for (std::size_t index = 0U; index < images.size(); ++index) {
        images[index] = load_tiff_gray8(options.paths.images_directory /
                                        kThreeImages[index].filename);
    }

    const std::size_t task_count = images.size() * options.k_values.size();
    std::vector<SweepThreeRow> rows(task_count);
    // sweep_k.m advances one rng(2026) stream through H, V and D for every
    // image-major task. Save task-boundary states before launching workers.
    MatlabTwister sampling_stream(2026U);
    std::vector<MatlabTwister> sampling_states;
    sampling_states.reserve(task_count);
    for (std::size_t task = 0U; task < task_count; ++task) {
        sampling_states.push_back(sampling_stream);
        discard_adjacent_calls(sampling_stream, 3U,
                               options.correlation_samples);
    }
    progress << images.size() << " images x " << options.k_values.size()
             << " K values = " << task_count << " native C++ tasks\n";
    ProgressCounter counter(progress, task_count, "three-image sweep");
    parallel_for(task_count, options.workers, [&](std::size_t task) {
        // sweep_k.m writes image-major rows; retaining that order also makes
        // output independent of worker scheduling.
        const std::size_t image_index = task / options.k_values.size();
        const std::size_t k_index = task % options.k_values.size();
        const std::size_t k = options.k_values[k_index];
        const Image& image = images[image_index];
        Bytes perturbed = image.pixels();
        perturb_center_lsb(perturbed, image.width(), image.height());
        const Bits key = secret_key(k);

        const auto started = Clock::now();
        const EncryptionResult first = encrypt_fast(image.pixels(), key);
        const EncryptionResult second = encrypt_fast(perturbed, key);
        const double seconds =
            std::chrono::duration<double>(Clock::now() - started).count();
        const std::size_t iterations = expected_iterations(image.size(), k);
        if (first.iterations != iterations || second.iterations != iterations) {
            throw std::logic_error("iteration count mismatch in three-image sweep");
        }

        SweepThreeRow row;
        row.image_index = image_index;
        row.k = k;
        row.entropy = shannon_entropy(first.cipher);
        MatlabTwister sampling = sampling_states[task];
        row.corr_h = adjacent_correlation(
            first.cipher, image.width(), image.height(),
            AdjacentDirection::Horizontal, options.correlation_samples,
            sampling).correlation;
        row.corr_v = adjacent_correlation(
            first.cipher, image.width(), image.height(),
            AdjacentDirection::Vertical, options.correlation_samples,
            sampling).correlation;
        row.corr_d = adjacent_correlation(
            first.cipher, image.width(), image.height(),
            AdjacentDirection::Diagonal, options.correlation_samples,
            sampling).correlation;
        const NpcrUaciResult change = npcr_uaci(first.cipher, second.cipher);
        row.npcr = change.npcr_percent;
        row.uaci = change.uaci_percent;
        row.seconds = seconds;
        rows[task] = row;
        counter.completed_one();
    });

    const fs::path csv_path = options.paths.results_directory / "sweep_k.csv";
    std::ofstream output = open_output(csv_path);
    output << "image,K,entropy_cipher,corrH_cipher,corrV_cipher,corrD_cipher,"
              "npcr,uaci,seconds\n";
    for (const SweepThreeRow& row : rows) {
        output << csv_escape(kThreeImages[row.image_index].short_name) << ','
               << row.k << ',' << fixed_number(row.entropy, 6) << ','
               << fixed_number(row.corr_h, 6) << ','
               << fixed_number(row.corr_v, 6) << ','
               << fixed_number(row.corr_d, 6) << ','
               << fixed_number(row.npcr, 6) << ','
               << fixed_number(row.uaci, 6) << ','
               << fixed_number(row.seconds, 4) << '\n';
    }
    finish_output(output, csv_path);
    const NpcrUaciResult ideal = npcr_uaci_ideal(8);
    std::vector<Panel> panels;
    panels.push_back(three_image_panel(
        rows, [](const SweepThreeRow& row) { return row.entropy; },
        "Cipher entropy", "Entropy (bits)", 8.0));
    panels.push_back(three_image_panel(
        rows, [](const SweepThreeRow& row) { return row.corr_h; },
        "Horizontal correlation", "Correlation", 0.0));
    panels.push_back(three_image_panel(
        rows, [](const SweepThreeRow& row) { return row.npcr; },
        "NPCR", "NPCR (%)", ideal.npcr_percent));
    panels.push_back(three_image_panel(
        rows, [](const SweepThreeRow& row) { return row.uaci; },
        "UACI", "UACI (%)", ideal.uaci_percent));
    panels.push_back(three_image_panel(
        rows, [](const SweepThreeRow& row) { return row.seconds; },
        "Two-encryption time", "Seconds"));
    const fs::path pdf_path = options.paths.results_directory / "sweep_k.pdf";
    write_plot_grid_pdf(pdf_path,
                        "xormap image cipher: metric vs keystream width K",
                        "three grayscale SIPI images; K grid evaluated in native C++",
                        panels, 2U);
    progress << "Wrote " << csv_path << '\n';
    progress << "Wrote " << pdf_path << '\n';
}

struct SweepAllComputation {
    std::vector<ManifestEntry> entries;
    std::vector<Image> images;
    std::vector<SweepAllRow> rows;
};

// Shared by sweep_all_images() and run_tests() so both compute identically.
[[nodiscard]] SweepAllComputation compute_sweep_all(const SweepOptions& options,
                                                     std::ostream& progress)
{
    validate_k_values(options.k_values);
    if (options.correlation_samples == 0U) {
        throw std::invalid_argument("correlation sample count must be positive");
    }
    const fs::path manifest = options.manifest_path.empty()
        ? options.paths.images_directory / "manifest_gray.csv"
        : options.manifest_path;
    std::vector<ManifestEntry> entries =
        read_gray_manifest(manifest, options.paths.images_directory);
    if (entries.empty()) {
        throw std::runtime_error("grayscale manifest contains no images");
    }
    std::vector<Image> images =
        load_manifest_images(entries, options.workers, progress);

    if (entries.size() > std::numeric_limits<std::size_t>::max() /
                             options.k_values.size()) {
        throw std::length_error("sweep task count overflows size_t");
    }
    const std::size_t task_count = entries.size() * options.k_values.size();
    if (task_count >
        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        throw std::length_error(
            "all-image sweep has more tasks than MATLAB rng(seed) supports");
    }
    std::vector<SweepAllRow> rows(task_count);
    progress << entries.size() << " images x " << options.k_values.size()
             << " K values = " << task_count << " native C++ tasks\n";
    ProgressCounter counter(progress, task_count, "all-image sweep");
    parallel_for(task_count, options.workers, [&](std::size_t task) {
        // MATLAB ndgrid followed by (:) produces K-major order.
        const std::size_t k_index = task / entries.size();
        const std::size_t image_index = task % entries.size();
        const std::size_t k = options.k_values[k_index];
        const Image& image = images[image_index];
        Bytes perturbed = image.pixels();
        perturb_center_lsb(perturbed, image.width(), image.height());
        // This call occurs inside MATLAB parfor. Workers retain their default
        // Threefry generator when secret_key.m executes rng(1729).
        const Bits key = worker_secret_key(k);

        const auto started = Clock::now();
        const EncryptionResult first = encrypt_fast(image.pixels(), key);
        const EncryptionResult second = encrypt_fast(perturbed, key);
        const double seconds =
            std::chrono::duration<double>(Clock::now() - started).count();
        const std::size_t iterations = expected_iterations(image.size(), k);
        if (first.iterations != second.iterations ||
            first.iterations != iterations) {
            throw std::runtime_error("iteration count mismatch for " +
                                     entries[image_index].name + " K=" +
                                     std::to_string(k));
        }

        SweepAllRow row;
        row.image_index = image_index;
        row.k = k;
        row.iterations = iterations;
        row.entropy = shannon_entropy(first.cipher);
        // rng(idx) inside MATLAB parfor keeps the worker's Threefry type.
        MatlabThreefry sampling(static_cast<std::uint32_t>(task + 1U));
        row.abs_corr_h = std::abs(adjacent_correlation(
            first.cipher, image.width(), image.height(),
            AdjacentDirection::Horizontal, options.correlation_samples,
            sampling).correlation);
        const NpcrUaciResult change = npcr_uaci(first.cipher, second.cipher);
        row.npcr = change.npcr_percent;
        row.uaci = change.uaci_percent;
        row.seconds = seconds;
        rows[task] = row;
        counter.completed_one();
    });
    return {std::move(entries), std::move(images), std::move(rows)};
}

void write_sweep_all_csv(const fs::path& csv_path,
                         const SweepAllComputation& computed)
{
    std::ofstream output = open_output(csv_path);
    output << "image,volume,K,height,width,num_pixels,iterations_per_encrypt,"
              "pixels_per_iteration,entropy_cipher,abs_corrH_cipher,npcr,uaci,"
              "seconds_two_encryptions\n";
    for (const SweepAllRow& row : computed.rows) {
        const ManifestEntry& entry = computed.entries[row.image_index];
        output << csv_escape(entry.name) << ',' << csv_escape(entry.volume) << ','
               << row.k << ',' << entry.height << ',' << entry.width << ','
               << computed.images[row.image_index].size() << ',' << row.iterations
               << ',';
        if (row.k % kBitsPerByte == 0U) {
            output << row.k / kBitsPerByte;
        } else {
            output << fixed_number(static_cast<double>(row.k) /
                                       static_cast<double>(kBitsPerByte),
                                   6);
        }
        output << ',' << fixed_number(row.entropy, 6) << ','
               << fixed_number(row.abs_corr_h, 6) << ','
               << fixed_number(row.npcr, 6) << ','
               << fixed_number(row.uaci, 6) << ','
               << fixed_number(row.seconds, 4) << '\n';
    }
    finish_output(output, csv_path);
}

[[nodiscard]] std::vector<Panel> build_sweep_all_panels(
    const std::vector<SweepAllRow>& rows)
{
    const NpcrUaciResult ideal = npcr_uaci_ideal(8);
    std::vector<Panel> panels;
    panels.push_back(scatter_mean_panel(
        rows, [](const SweepAllRow& row) { return row.entropy; },
        "Cipher entropy", "Entropy (bits)", kOrange, 8.0));
    panels.push_back(scatter_mean_panel(
        rows, [](const SweepAllRow& row) { return row.abs_corr_h; },
        "Horizontal correlation", "Mean |correlation|", kOrange, 0.0));
    panels.push_back(combined_npcr_uaci_panel(
        rows, [](const SweepAllRow& row) { return row.npcr; }, ideal.npcr_percent,
        [](const SweepAllRow& row) { return row.uaci; }, ideal.uaci_percent,
        "NPCR/UACI (plaintext bit flip)"));
    return panels;
}

void sweep_all_images(const SweepOptions& options, std::ostream& progress,
                      std::vector<Panel>* panels_out)
{
    const SweepAllComputation computed = compute_sweep_all(options, progress);
    make_directory(options.paths.results_directory);
    const fs::path csv_path =
        options.paths.results_directory / "sweep_k_gray_all.csv";
    write_sweep_all_csv(csv_path, computed);
    progress << "Wrote " << csv_path << '\n';

    std::vector<Panel> panels = build_sweep_all_panels(computed.rows);
    if (panels_out != nullptr) {
        panels_out->insert(panels_out->end(), panels.begin(), panels.end());
        return;
    }
    const fs::path pdf_path =
        options.paths.results_directory / "sweep_k_gray_all.pdf";
    write_plot_grid_pdf(
        pdf_path, "xormap grayscale: every SIPI grayscale image",
        std::to_string(computed.entries.size()) +
            " images; native C++ parallel sweep; faint points are images",
        panels, 2U);
    progress << "Wrote " << pdf_path << '\n';
}

struct AnalysisComputation {
    std::vector<ManifestEntry> entries;
    std::vector<Image> images;
    std::vector<AnalysisRow> rows;
    std::vector<BitStudyRow> bit_rows;
    std::size_t histogram_passes = 0U;
};

// Shared by analyze_all_images() and run_tests() so both compute identically.
[[nodiscard]] AnalysisComputation compute_analysis_all(const AnalysisOptions& options,
                                                        std::ostream& progress)
{
    validate_k_values(options.k_values);
    const fs::path manifest = options.manifest_path.empty()
        ? options.paths.images_directory / "manifest_gray.csv"
        : options.manifest_path;
    std::vector<ManifestEntry> entries =
        read_gray_manifest(manifest, options.paths.images_directory);
    if (entries.empty()) {
        throw std::runtime_error("grayscale manifest contains no images");
    }
    std::vector<Image> images =
        load_manifest_images(entries, options.workers, progress);

    std::vector<ChiSquareResult> plain_chi(entries.size());
    parallel_for(entries.size(), options.workers, [&](std::size_t index) {
        plain_chi[index] = chi_square_uniformity(images[index].pixels());
    });

    if (entries.size() > std::numeric_limits<std::size_t>::max() /
                             options.k_values.size()) {
        throw std::length_error("analysis task count overflows size_t");
    }
    const std::size_t task_count = entries.size() * options.k_values.size();
    std::vector<AnalysisRow> rows(task_count);
    progress << entries.size() << " images x " << options.k_values.size()
             << " K values = " << task_count << " native C++ analysis tasks\n";
    ProgressCounter counter(progress, task_count, "analysis main pass");
    parallel_for(task_count, options.workers, [&](std::size_t task) {
        const std::size_t k_index = task / entries.size();
        const std::size_t image_index = task % entries.size();
        const std::size_t k = options.k_values[k_index];
        const Image& image = images[image_index];
        // The main analysis table is a parfor in analysis_gray.m, so its
        // secret_key call is seeded on the worker's Threefry stream.
        Bits key1 = worker_secret_key(k);
        Bits key2 = key1;
        key2[0] ^= Byte{1};  // MATLAB FLIP_BIT = 1.

        const EncryptionResult first = encrypt_fast(image.pixels(), key1);
        const EncryptionResult second = encrypt_fast(image.pixels(), key2);
        const std::size_t iterations = expected_iterations(image.size(), k);
        if (first.iterations != iterations || second.iterations != iterations) {
            throw std::runtime_error("iteration count mismatch for analysis task");
        }
        const Bytes difference = xor_bytes(first.cipher, second.cipher);
        const Bytes wrong = xor_bytes(image.pixels(), difference);

        // Check the shortcut against the actual canonical decrypt once, just
        // as analysis_gray.m does for parfor task 1.
        if (task == 0U) {
            const Bytes actual_wrong = decrypt_canonical(first.cipher, second.seed);
            if (actual_wrong != wrong) {
                throw std::runtime_error(
                    "wrong-key shortcut disagrees with decrypt()" );
            }
        }
        const Bytes recovered = decrypt_fast(first.cipher, first.seed);
        if (recovered != image.pixels()) {
            throw std::runtime_error("round trip failed for " +
                                     entries[image_index].name + " K=" +
                                     std::to_string(k));
        }

        const NpcrUaciResult sensitivity = npcr_uaci(first.cipher, second.cipher);
        const ChiSquareResult cipher_chi = chi_square_uniformity(first.cipher);
        AnalysisRow row;
        row.image_index = image_index;
        row.k = k;
        row.num_pixels = image.size();
        row.npcr_key = sensitivity.npcr_percent;
        row.uaci_key = sensitivity.uaci_percent;
        row.psnr_cipher_pair = psnr_db(first.cipher, second.cipher).psnr_db;
        row.psnr_plain_wrong_key = psnr_db(image.pixels(), wrong).psnr_db;
        row.chi2_plain = plain_chi[image_index].statistic;
        row.chi2_cipher = cipher_chi.statistic;
        row.chi2_critical = cipher_chi.critical_value;
        row.psnr_plain_cipher = psnr_db(image.pixels(), first.cipher).psnr_db;
        row.psnr_roundtrip = psnr_db(image.pixels(), recovered).psnr_db;
        row.checksum = diff_checksum(difference);
        row.first_difference = first_differing_byte(difference);
        if (!std::isinf(row.psnr_roundtrip) || row.psnr_roundtrip < 0.0) {
            throw std::runtime_error("exact round trip did not produce +Inf PSNR");
        }
        rows[task] = row;
        counter.completed_one();
    });

    // For a linear XOR map, the ciphertext XOR difference for a fixed key
    // flip is image-independent.  This is a hard assertion, not just a note.
    for (std::size_t k_index = 0U; k_index < options.k_values.size(); ++k_index) {
        const std::size_t offset = k_index * entries.size();
        const std::uint32_t expected = rows[offset].checksum;
        for (std::size_t image_index = 1U; image_index < entries.size(); ++image_index) {
            if (rows[offset + image_index].checksum != expected) {
                throw std::runtime_error(
                    "ciphertext key-difference checksum depends on image at K=" +
                    std::to_string(options.k_values[k_index]));
            }
        }
    }
    progress << "Key-difference image-independence confirmed for all "
             << options.k_values.size() << " K values.\n";

    struct BitTask {
        std::size_t k_index;
        std::size_t bit;
    };
    std::vector<BitTask> bit_tasks;
    for (std::size_t k_index = 0U; k_index < options.k_values.size(); ++k_index) {
        for (const std::size_t bit : spread_bit_positions(options.k_values[k_index])) {
            bit_tasks.push_back({k_index, bit});
        }
    }

    struct BitBase {
        Bits key;
        Bytes cipher;
    };
    std::vector<BitBase> bases(options.k_values.size());
    parallel_for(options.k_values.size(), options.workers, [&](std::size_t k_index) {
        BitBase base;
        base.key = secret_key(options.k_values[k_index]);
        base.cipher = encrypt_fast(images.front().pixels(), base.key).cipher;
        bases[k_index] = std::move(base);
    });

    std::vector<BitStudyRow> bit_rows(bit_tasks.size());
    ProgressCounter bit_counter(progress, bit_tasks.size(), "per-bit study");
    parallel_for(bit_tasks.size(), options.workers, [&](std::size_t task) {
        const BitTask bit_task = bit_tasks[task];
        const std::size_t k = options.k_values[bit_task.k_index];
        Bits key2 = bases[bit_task.k_index].key;
        key2[bit_task.bit - 1U] ^= Byte{1};
        const Bytes cipher2 = encrypt_fast(images.front().pixels(), key2).cipher;
        const NpcrUaciResult sensitivity =
            npcr_uaci(bases[bit_task.k_index].cipher, cipher2);
        BitStudyRow row;
        row.k = k;
        row.bit = bit_task.bit;
        row.npcr = sensitivity.npcr_percent;
        row.uaci = sensitivity.uaci_percent;
        row.psnr = psnr_db(bases[bit_task.k_index].cipher, cipher2).psnr_db;
        bit_rows[task] = row;
        bit_counter.completed_one();
    });

    std::size_t histogram_passes = 0U;
    for (const AnalysisRow& row : rows) {
        if (row.chi2_cipher <= row.chi2_critical) {
            ++histogram_passes;
        }
    }
    return {std::move(entries), std::move(images), std::move(rows),
            std::move(bit_rows), histogram_passes};
}

void write_analysis_csvs(const AnalysisOptions& options,
                         const AnalysisComputation& computed,
                         std::ostream& progress)
{
    const fs::path key_path =
        options.paths.results_directory / "key_sensitivity_gray.csv";
    std::ofstream key_output = open_output(key_path);
    key_output << "image,volume,K,num_pixels,flipped_key_bit,npcr_key,uaci_key,"
                  "psnr_cipher_pair_db,psnr_plain_wrongkey_db,first_differing_byte,"
                  "key_diff_checksum\n";
    for (const AnalysisRow& row : computed.rows) {
        const ManifestEntry& entry = computed.entries[row.image_index];
        key_output << csv_escape(entry.name) << ',' << csv_escape(entry.volume) << ','
                   << row.k << ',' << row.num_pixels << ",1,"
                   << fixed_number(row.npcr_key, 6) << ','
                   << fixed_number(row.uaci_key, 6) << ','
                   << fixed_number(row.psnr_cipher_pair, 4) << ','
                   << fixed_number(row.psnr_plain_wrong_key, 4) << ','
                   << row.first_difference << ',' << row.checksum << '\n';
    }
    finish_output(key_output, key_path);

    const fs::path bits_path =
        options.paths.results_directory / "key_sensitivity_bits_gray.csv";
    std::ofstream bits_output = open_output(bits_path);
    bits_output << "K,flipped_key_bit,npcr_key,uaci_key,psnr_cipher_pair_db\n";
    for (const BitStudyRow& row : computed.bit_rows) {
        bits_output << row.k << ',' << row.bit << ','
                    << fixed_number(row.npcr, 6) << ','
                    << fixed_number(row.uaci, 6) << ','
                    << fixed_number(row.psnr, 4) << '\n';
    }
    finish_output(bits_output, bits_path);

    const fs::path histogram_path =
        options.paths.results_directory / "histogram_analysis_gray.csv";
    std::ofstream histogram_output = open_output(histogram_path);
    histogram_output << "image,volume,K,num_pixels,chi2_plain,chi2_cipher,"
                         "chi2_critical_005,cipher_uniform_pass\n";
    for (const AnalysisRow& row : computed.rows) {
        const ManifestEntry& entry = computed.entries[row.image_index];
        const bool passes = row.chi2_cipher <= row.chi2_critical;
        histogram_output << csv_escape(entry.name) << ','
                         << csv_escape(entry.volume) << ',' << row.k << ','
                         << row.num_pixels << ',' << fixed_number(row.chi2_plain, 4)
                         << ',' << fixed_number(row.chi2_cipher, 4) << ','
                         << fixed_number(row.chi2_critical, 4) << ','
                         << (passes ? 1 : 0) << '\n';
    }
    finish_output(histogram_output, histogram_path);

    const fs::path psnr_path =
        options.paths.results_directory / "psnr_analysis_gray.csv";
    std::ofstream psnr_output = open_output(psnr_path);
    psnr_output << "image,volume,K,num_pixels,psnr_plain_cipher_db,"
                   "psnr_plain_wrongkey_db,psnr_roundtrip_db\n";
    for (const AnalysisRow& row : computed.rows) {
        const ManifestEntry& entry = computed.entries[row.image_index];
        psnr_output << csv_escape(entry.name) << ',' << csv_escape(entry.volume)
                    << ',' << row.k << ',' << row.num_pixels << ','
                    << fixed_number(row.psnr_plain_cipher, 4) << ','
                    << fixed_number(row.psnr_plain_wrong_key, 4) << ','
                    << fixed_number(row.psnr_roundtrip, 4) << '\n';
    }
    finish_output(psnr_output, psnr_path);

    const double pass_rate = 100.0 * static_cast<double>(computed.histogram_passes) /
                             static_cast<double>(computed.rows.size());
    progress << "Cipher histograms passing chi-square at alpha=0.05: "
             << fixed_number(pass_rate, 1) << "%\n"
             << "Round-trip PSNR is +Inf in " << computed.rows.size() << " of "
             << computed.rows.size() << " cases.\n"
             << "Wrote " << key_path << '\n'
             << "Wrote " << bits_path << '\n'
             << "Wrote " << histogram_path << '\n'
             << "Wrote " << psnr_path << '\n';
}

struct AnalysisPanels {
    std::vector<Panel> key_panels;
    std::vector<Panel> histogram_panels;
    std::vector<Panel> psnr_panels;
};

[[nodiscard]] AnalysisPanels build_analysis_panels(
    const AnalysisComputation& computed, std::size_t example_k)
{
    const NpcrUaciResult ideal = npcr_uaci_ideal(8);
    AnalysisPanels result;
    result.key_panels.push_back(combined_npcr_uaci_panel(
        computed.rows, [](const AnalysisRow& row) { return row.npcr_key; },
        ideal.npcr_percent,
        [](const AnalysisRow& row) { return row.uaci_key; }, ideal.uaci_percent,
        "NPCR/UACI (key bit flip)"));
    result.key_panels.push_back(scatter_mean_panel(
        computed.rows,
        [](const AnalysisRow& row) { return row.psnr_plain_wrong_key; },
        "Wrong-key decryption", "PSNR (dB)", kBlue));
    result.key_panels.push_back(scatter_mean_panel(
        computed.bit_rows, [](const BitStudyRow& row) { return row.npcr; },
        "By flipped key-bit position", "NPCR (%)", kBlue,
        ideal.npcr_percent, "flipped bit positions"));

    result.histogram_panels.push_back(scatter_mean_panel(
        computed.rows, [](const AnalysisRow& row) { return row.chi2_cipher; },
        "Cipher chi-square", "Chi-square (255 dof)", kBlue,
        computed.rows.front().chi2_critical));
    result.histogram_panels.push_back(make_log_chi_square_distribution_panel(
        computed.rows, computed.rows.front().chi2_critical));

    const Bytes example_cipher =
        encrypt_fast(computed.images.front().pixels(), secret_key(example_k)).cipher;
    result.histogram_panels.push_back(make_histogram_panel(
        "Cipher histogram (K=" + std::to_string(example_k) + ")",
        example_cipher, kBlue));

    result.psnr_panels.push_back(scatter_mean_panel(
        computed.rows, [](const AnalysisRow& row) { return row.psnr_plain_cipher; },
        "Plain versus cipher", "PSNR (dB)", kBlue));
    result.psnr_panels.push_back(scatter_mean_panel(
        computed.rows,
        [](const AnalysisRow& row) { return row.psnr_plain_wrong_key; },
        "Plain versus wrong-key decrypt", "PSNR (dB)", kBlue));
    return result;
}

void analyze_all_images(const AnalysisOptions& options, std::ostream& progress,
                        std::vector<Panel>* panels_out)
{
    const AnalysisComputation computed = compute_analysis_all(options, progress);
    make_directory(options.paths.results_directory);
    write_analysis_csvs(options, computed, progress);

    const AnalysisPanels panels =
        build_analysis_panels(computed, options.k_values.back());
    if (panels_out != nullptr) {
        panels_out->insert(panels_out->end(), panels.key_panels.begin(),
                           panels.key_panels.end());
        panels_out->insert(panels_out->end(), panels.histogram_panels.begin(),
                           panels.histogram_panels.end());
        panels_out->insert(panels_out->end(), panels.psnr_panels.begin(),
                           panels.psnr_panels.end());
        return;
    }

    const fs::path key_pdf =
        options.paths.results_directory / "key_sensitivity_gray.pdf";
    write_plot_grid_pdf(
        key_pdf, "Key sensitivity: grayscale, all SIPI grayscale images",
        "one flipped key bit; image-independent XOR-difference checks passed",
        panels.key_panels, 2U);

    const fs::path histogram_pdf =
        options.paths.results_directory / "histogram_analysis_gray.pdf";
    write_plot_grid_pdf(
        histogram_pdf, "Histogram analysis: grayscale",
        "chi-square uniformity over 256 levels; native C++ vector report",
        panels.histogram_panels, 2U);

    const fs::path psnr_pdf =
        options.paths.results_directory / "psnr_analysis_gray.pdf";
    write_plot_grid_pdf(
        psnr_pdf, "PSNR: grayscale, all SIPI grayscale images",
        "low means plaintext is not recoverable; correct round-trip is +Inf",
        panels.psnr_panels, 2U);

    progress << "Wrote " << key_pdf << '\n'
             << "Wrote " << histogram_pdf << '\n'
             << "Wrote " << psnr_pdf << '\n';
}

void run_tests(const SweepOptions& sweep_options,
               const AnalysisOptions& analysis_options,
               std::ostream& progress)
{
    const SweepAllComputation sweep_computed =
        compute_sweep_all(sweep_options, progress);
    const AnalysisComputation analysis_computed =
        compute_analysis_all(analysis_options, progress);
    if (sweep_computed.rows.size() != analysis_computed.rows.size()) {
        throw std::runtime_error(
            "run-tests: sweep and analysis passes produced different task counts");
    }

    make_directory(sweep_options.paths.results_directory);
    const fs::path csv_path =
        sweep_options.paths.results_directory / "sweep_k_gray_all.csv";
    std::ofstream csv = open_output(csv_path);
    csv << "image,volume,K,height,width,num_pixels,iterations_per_encrypt,"
           "pixels_per_iteration,entropy_cipher,abs_corrH_cipher,"
           "npcr_plaintext_flip,uaci_plaintext_flip,"
           "flipped_key_bit,npcr_key_flip,uaci_key_flip,psnr_cipher_pair_db,"
           "psnr_plain_wrongkey_db,first_differing_byte,key_diff_checksum,"
           "chi2_plain,chi2_cipher,chi2_critical_005,cipher_uniform_pass,"
           "psnr_plain_cipher_db,psnr_roundtrip_db\n";
    // sweep_computed.rows[i] and analysis_computed.rows[i] are the same
    // (image,K) task: both passes iterate the same images x k_values in
    // identical K-major order (see compute_sweep_all/compute_analysis_all).
    for (std::size_t i = 0; i < sweep_computed.rows.size(); ++i) {
        const SweepAllRow& sweep_row = sweep_computed.rows[i];
        const AnalysisRow& analysis_row = analysis_computed.rows[i];
        const ManifestEntry& entry = sweep_computed.entries[sweep_row.image_index];
        csv << csv_escape(entry.name) << ',' << csv_escape(entry.volume) << ','
            << sweep_row.k << ',' << entry.height << ',' << entry.width << ','
            << sweep_computed.images[sweep_row.image_index].size() << ','
            << sweep_row.iterations << ',';
        if (sweep_row.k % kBitsPerByte == 0U) {
            csv << sweep_row.k / kBitsPerByte;
        } else {
            csv << fixed_number(static_cast<double>(sweep_row.k) /
                                    static_cast<double>(kBitsPerByte), 6);
        }
        csv << ',' << fixed_number(sweep_row.entropy, 6) << ','
            << fixed_number(sweep_row.abs_corr_h, 6) << ','
            << fixed_number(sweep_row.npcr, 6) << ','
            << fixed_number(sweep_row.uaci, 6) << ",1,"
            << fixed_number(analysis_row.npcr_key, 6) << ','
            << fixed_number(analysis_row.uaci_key, 6) << ','
            << fixed_number(analysis_row.psnr_cipher_pair, 4) << ','
            << fixed_number(analysis_row.psnr_plain_wrong_key, 4) << ','
            << analysis_row.first_difference << ',' << analysis_row.checksum
            << ',' << fixed_number(analysis_row.chi2_plain, 4) << ','
            << fixed_number(analysis_row.chi2_cipher, 4) << ','
            << fixed_number(analysis_row.chi2_critical, 4) << ','
            << (analysis_row.chi2_cipher <= analysis_row.chi2_critical ? 1 : 0)
            << ',' << fixed_number(analysis_row.psnr_plain_cipher, 4) << ','
            << fixed_number(analysis_row.psnr_roundtrip, 4) << '\n';
    }
    csv << '\n';
    csv << "K,flipped_key_bit,npcr_key_bitstudy,uaci_key_bitstudy,"
           "psnr_cipher_pair_bitstudy_db\n";
    for (const BitStudyRow& row : analysis_computed.bit_rows) {
        csv << row.k << ',' << row.bit << ',' << fixed_number(row.npcr, 6) << ','
            << fixed_number(row.uaci, 6) << ',' << fixed_number(row.psnr, 4) << '\n';
    }
    finish_output(csv, csv_path);
    progress << "Wrote " << csv_path << '\n';

    std::vector<Panel> panels = build_sweep_all_panels(sweep_computed.rows);
    const AnalysisPanels analysis_panels = build_analysis_panels(
        analysis_computed, analysis_options.k_values.back());
    panels.insert(panels.end(), analysis_panels.key_panels.begin(),
                  analysis_panels.key_panels.end());
    panels.insert(panels.end(), analysis_panels.histogram_panels.begin(),
                  analysis_panels.histogram_panels.end());
    panels.insert(panels.end(), analysis_panels.psnr_panels.begin(),
                  analysis_panels.psnr_panels.end());

    const fs::path pdf_path =
        sweep_options.paths.results_directory / "sweep_k_gray_all.pdf";
    write_plot_grid_pdf(
        pdf_path, "xormap grayscale: every SIPI grayscale image — full test report",
        "sweep (entropy/correlation/plaintext-bit diffusion) + "
        "key-bit sensitivity + histogram + PSNR, native C++ parallel",
        panels, 3U);
    progress << "Combined all " << panels.size()
             << " panels into one " << pdf_path << '\n';
}

void download_images(const DownloadOptions& options, std::ostream& progress)
{
    make_directory(options.images_directory);
    const std::vector<SipiImageSpec>& specs = options.all_images
        ? all_grayscale_images()
        : basic_grayscale_images();

    struct DownloadRow {
        fs::path path;
        bool existed = false;
        std::optional<Image> image;
        std::string error;
    };
    std::vector<DownloadRow> rows(specs.size());
    std::mutex progress_mutex;
    std::atomic<std::size_t> completed{0U};
    progress << "Downloading/validating " << specs.size()
             << " grayscale TIFF images with native C++ workers.\n";
    parallel_for(specs.size(), options.workers, [&](std::size_t index) {
        DownloadRow row;
        row.path = options.images_directory / (specs[index].name + ".tiff");
        row.existed = fs::is_regular_file(row.path) && !options.overwrite;
        try {
            row.path = download_sipi_image(specs[index], options.images_directory,
                                           options.overwrite);
            row.image = load_tiff_gray8(row.path);
        } catch (const std::exception& error) {
            row.error = error.what();
        }
        rows[index] = std::move(row);
        const std::size_t done = completed.fetch_add(1U) + 1U;
        std::lock_guard<std::mutex> lock(progress_mutex);
        progress << '[' << std::setw(3) << done << '/' << std::setw(3)
                 << specs.size() << "] " << specs[index].name << " ("
                 << specs[index].volume << ") "
                 << (rows[index].error.empty()
                         ? (rows[index].existed ? "present and valid" :
                                                   "downloaded and valid")
                         : "FAILED: " + rows[index].error)
                 << '\n';
        progress.flush();
    });

    std::size_t valid = 0U;
    std::size_t total_pixels = 0U;
    std::size_t failed = 0U;
    for (const DownloadRow& row : rows) {
        if (!row.image) {
            ++failed;
            continue;
        }
        ++valid;
        if (row.image->size() >
            std::numeric_limits<std::size_t>::max() - total_pixels) {
            throw std::length_error("downloaded image pixel total overflows size_t");
        }
        total_pixels += row.image->size();
    }

    progress << valid << " of " << specs.size()
             << " images are native 8-bit grayscale, "
             << fixed_number(static_cast<double>(total_pixels) / 1.0e6, 2)
             << "M pixels total.\n";
    if (failed != 0U) {
        progress << "Rejected " << failed << " failed or invalid image(s); "
                 << "no manifest was published.\n";
        throw std::runtime_error(std::to_string(failed) + " of " +
                                 std::to_string(specs.size()) +
                                 " image download(s) failed validation");
    }

    if (options.all_images) {
        const fs::path manifest_path =
            options.images_directory / "manifest_gray.csv";
        std::ofstream manifest = open_output(manifest_path);
        manifest << "filename,volume,name,height,width,channels\n";
        for (std::size_t index = 0U; index < specs.size(); ++index) {
            if (!rows[index].image) {
                continue;
            }
            manifest << csv_escape(specs[index].name + ".tiff") << ','
                     << csv_escape(specs[index].volume) << ','
                     << csv_escape(specs[index].name) << ','
                     << rows[index].image->height() << ','
                     << rows[index].image->width() << ",1\n";
        }
        finish_output(manifest, manifest_path);
        progress << "Wrote " << manifest_path << '\n';
    }
}

NormalizedSweep read_sweep_csv(const fs::path& path, std::string display_name)
{
    const std::vector<CsvRecord> records = parse_csv(path);
    const CsvRecord& header = records.front();
    const auto columns = header_map(header, path);

    const bool is_rgb = columns.count("mean_entropy_cipher") != 0U &&
                        (columns.count("npcr_plaintext_flip_packed") != 0U ||
                         columns.count("npcr_packed") != 0U);
    const bool is_gray = columns.count("entropy_cipher") != 0U &&
                         (columns.count("npcr_plaintext_flip") != 0U ||
                          columns.count("npcr") != 0U);
    if (is_rgb == is_gray) {
        csv_error(path, header.line,
                  "unrecognized or ambiguous sweep CSV schema");
    }

    const std::size_t image_column = require_column(columns, "image", path,
                                                     header.line);
    const auto volume_found = columns.find("volume");
    const std::size_t k_column = require_column(columns, "K", path, header.line);
    const std::size_t height_column = require_column(columns, "height", path,
                                                      header.line);
    const std::size_t width_column = require_column(columns, "width", path,
                                                     header.line);
    const std::size_t pixels_column = require_column(columns, "num_pixels", path,
                                                      header.line);
    const std::size_t entropy_column = require_column(
        columns, is_rgb ? "mean_entropy_cipher" : "entropy_cipher", path,
        header.line);
    const std::size_t corr_column = require_column(
        columns, is_rgb ? "mean_abs_corrH_cipher" : "abs_corrH_cipher", path,
        header.line);
    // Prefer the plaintext-bit-flip columns (current schema); fall back to
    // the pre-consolidation bare names for old CSVs still on disk.
    const std::size_t npcr_column = require_column(
        columns,
        columns.count(is_rgb ? "npcr_plaintext_flip_packed" : "npcr_plaintext_flip")
            ? (is_rgb ? "npcr_plaintext_flip_packed" : "npcr_plaintext_flip")
            : (is_rgb ? "npcr_packed" : "npcr"),
        path, header.line);
    const std::size_t uaci_column = require_column(
        columns,
        columns.count(is_rgb ? "uaci_plaintext_flip_packed" : "uaci_plaintext_flip")
            ? (is_rgb ? "uaci_plaintext_flip_packed" : "uaci_plaintext_flip")
            : (is_rgb ? "uaci_packed" : "uaci"),
        path, header.line);
    // Encryption/decryption timing is no longer collected; treat it as
    // optional so both old (with timing) and new CSVs read cleanly.
    const auto seconds_found = columns.find(is_rgb ? "seconds" : "seconds_two_encryptions");

    NormalizedSweep sweep;
    sweep.rows.reserve(records.size() - 1U);
    for (std::size_t record_index = 1U; record_index < records.size();
         ++record_index) {
        const CsvRecord& record = records[record_index];
        if (record.fields.size() != header.fields.size()) {
            csv_error(path, record.line,
                      "field count differs from the header");
        }
        NormalizedSweepRow row;
        row.image = record.fields[image_column];
        row.volume = volume_found == columns.end()
            ? std::string{}
            : record.fields[volume_found->second];
        if (row.image.empty()) {
            csv_error(path, record.line, "image must not be empty");
        }
        row.k = parse_size(record.fields[k_column], path, record.line, "K");
        row.height = parse_size(record.fields[height_column], path, record.line,
                                "height");
        row.width = parse_size(record.fields[width_column], path, record.line,
                               "width");
        row.num_pixels = parse_size(record.fields[pixels_column], path,
                                    record.line, "num_pixels");
        if (row.height == 0U || row.width == 0U ||
            row.height > std::numeric_limits<std::size_t>::max() / row.width ||
            row.height * row.width != row.num_pixels) {
            csv_error(path, record.line,
                      "height * width does not equal num_pixels");
        }
        row.entropy = parse_double(record.fields[entropy_column], path,
                                   record.line, header.fields[entropy_column]);
        row.corr_h = parse_double(record.fields[corr_column], path, record.line,
                                  header.fields[corr_column]);
        row.npcr = parse_double(record.fields[npcr_column], path, record.line,
                                header.fields[npcr_column]);
        row.uaci = parse_double(record.fields[uaci_column], path, record.line,
                                header.fields[uaci_column]);
        row.seconds = seconds_found == columns.end()
            ? std::numeric_limits<double>::quiet_NaN()
            : parse_double(record.fields[seconds_found->second], path,
                           record.line, header.fields[seconds_found->second]);
        sweep.rows.push_back(std::move(row));
    }
    if (sweep.rows.empty()) {
        csv_error(path, header.line, "sweep contains no data rows");
    }
    std::stable_sort(sweep.rows.begin(), sweep.rows.end(),
                     [](const NormalizedSweepRow& first,
                        const NormalizedSweepRow& second) {
                         if (first.k != second.k) {
                             return first.k < second.k;
                         }
                         if (first.image != second.image) {
                             return first.image < second.image;
                         }
                         return first.volume < second.volume;
                     });

    sweep.metadata.name = display_name.empty() ? path.stem().string()
                                                : std::move(display_name);
    sweep.metadata.path = path;
    sweep.metadata.symbol_bits = is_rgb ? 24U : 8U;
    const NpcrUaciResult ideal =
        npcr_uaci_ideal(static_cast<int>(sweep.metadata.symbol_bits));
    sweep.metadata.npcr_ideal = ideal.npcr_percent;
    sweep.metadata.uaci_ideal = ideal.uaci_percent;
    std::set<std::string> image_names;
    std::set<std::size_t> k_values;
    for (const NormalizedSweepRow& row : sweep.rows) {
        image_names.insert(row.image);
        k_values.insert(row.k);
    }
    sweep.metadata.num_images = image_names.size();
    sweep.metadata.k_values.assign(k_values.begin(), k_values.end());
    return sweep;
}

void print_sweep_summary(const NormalizedSweep& sweep, std::ostream& output)
{
    if (sweep.rows.empty()) {
        throw std::invalid_argument("cannot summarize an empty sweep");
    }
    const auto groups = means_by_k(sweep);
    output << '\n' << sweep.metadata.name << '\n'
           << "  " << sweep.metadata.path << '\n'
           << "  " << sweep.metadata.num_images << " images x "
           << groups.size() << " K values = " << sweep.rows.size()
           << " rows\n"
           << "  NPCR/UACI measured on " << sweep.metadata.symbol_bits
           << "-bit symbols (ideal NPCR "
           << fixed_number(sweep.metadata.npcr_ideal, 5) << "%, UACI "
           << fixed_number(sweep.metadata.uaci_ideal, 4) << "%)\n\n"
           << "      K    entropy     |corrH|       NPCR %      UACI %        sec\n"
           << "  ----- ---------- ----------- ------------ ----------- ----------\n";

    double all_entropy = 0.0;
    double all_corr = 0.0;
    double all_npcr = 0.0;
    double all_uaci = 0.0;
    for (const auto& entry : groups) {
        const SweepMeans& group = entry.second;
        output << "  " << std::setw(5) << entry.first << ' '
               << std::setw(10) << fixed_number(group.entropy, 6) << ' '
               << std::setw(11) << fixed_number(group.corr_h, 6) << ' '
               << std::setw(12) << fixed_number(group.npcr, 6) << ' '
               << std::setw(11) << fixed_number(group.uaci, 6) << ' '
               << std::setw(10) << fixed_number(group.seconds, 1) << '\n';
    }
    for (const NormalizedSweepRow& row : sweep.rows) {
        all_entropy += row.entropy;
        all_corr += row.corr_h;
        all_npcr += row.npcr;
        all_uaci += row.uaci;
    }
    const double count = static_cast<double>(sweep.rows.size());
    output << "\n  deviation of the all-K mean from ideal: entropy "
           << fixed_number(all_entropy / count - 8.0, 6, true)
           << ", |corrH| " << fixed_number(all_corr / count, 6, true)
           << ", NPCR "
           << fixed_number(all_npcr / count - sweep.metadata.npcr_ideal,
                           6, true)
           << ", UACI "
           << fixed_number(all_uaci / count - sweep.metadata.uaci_ideal,
                           6, true)
           << "\n\n";
}

void compare_sweeps(const NormalizedSweep& rgb,
                    const NormalizedSweep& gray,
                    const fs::path& output_csv)
{
    if (rgb.rows.empty() || gray.rows.empty()) {
        throw std::invalid_argument("both comparison sweeps must contain rows");
    }
    const auto rgb_groups = means_by_k(rgb);
    const auto gray_groups = means_by_k(gray);
    if (rgb_groups.size() != gray_groups.size()) {
        throw std::invalid_argument("comparison sweeps do not have the same K grid");
    }
    for (const auto& entry : rgb_groups) {
        if (gray_groups.count(entry.first) == 0U) {
            throw std::invalid_argument("comparison sweeps do not have the same K grid");
        }
    }
    if (!output_csv.parent_path().empty()) {
        make_directory(output_csv.parent_path());
    }
    std::ofstream output = open_output(output_csv);
    output << "K,rgb_rows,gray_rows,rgb_mean_entropy,gray_mean_entropy,"
              "rgb_mean_abs_corrH,gray_mean_abs_corrH,rgb_mean_npcr,rgb_npcr_ideal,"
              "rgb_npcr_deviation,gray_mean_npcr,gray_npcr_ideal,gray_npcr_deviation,"
              "rgb_mean_uaci,rgb_uaci_ideal,rgb_uaci_deviation,gray_mean_uaci,"
              "gray_uaci_ideal,gray_uaci_deviation,rgb_seconds_total,"
              "gray_seconds_total\n";
    for (const auto& entry : rgb_groups) {
        const std::size_t k = entry.first;
        const SweepMeans& rgb_mean = entry.second;
        const SweepMeans& gray_mean = gray_groups.at(k);
        output << k << ',' << rgb_mean.count << ',' << gray_mean.count << ','
               << fixed_number(rgb_mean.entropy, 6) << ','
               << fixed_number(gray_mean.entropy, 6) << ','
               << fixed_number(rgb_mean.corr_h, 6) << ','
               << fixed_number(gray_mean.corr_h, 6) << ','
               << fixed_number(rgb_mean.npcr, 6) << ','
               << fixed_number(rgb.metadata.npcr_ideal, 6) << ','
               << fixed_number(rgb_mean.npcr - rgb.metadata.npcr_ideal, 6)
               << ',' << fixed_number(gray_mean.npcr, 6) << ','
               << fixed_number(gray.metadata.npcr_ideal, 6) << ','
               << fixed_number(gray_mean.npcr - gray.metadata.npcr_ideal, 6)
               << ',' << fixed_number(rgb_mean.uaci, 6) << ','
               << fixed_number(rgb.metadata.uaci_ideal, 6) << ','
               << fixed_number(rgb_mean.uaci - rgb.metadata.uaci_ideal, 6)
               << ',' << fixed_number(gray_mean.uaci, 6) << ','
               << fixed_number(gray.metadata.uaci_ideal, 6) << ','
               << fixed_number(gray_mean.uaci - gray.metadata.uaci_ideal, 6)
               << ',' << fixed_number(rgb_mean.seconds, 4) << ','
               << fixed_number(gray_mean.seconds, 4) << '\n';
    }
    finish_output(output, output_csv);

    const fs::path report_directory = output_csv.parent_path().empty()
        ? fs::path(".")
        : output_csv.parent_path();
    const std::string rgb_label = "RGB888: " +
                                  std::to_string(rgb.metadata.num_images) +
                                  " SIPI colour images";
    const std::string gray_label = "Grayscale: " +
                                   std::to_string(gray.metadata.num_images) +
                                   " SIPI grayscale images";

    Panel rgb_entropy = scatter_mean_panel(
        rgb.rows, [](const NormalizedSweepRow& row) { return row.entropy; },
        rgb_label, "Entropy (bits/channel)", kBlue, 8.0);
    Panel gray_entropy = scatter_mean_panel(
        gray.rows, [](const NormalizedSweepRow& row) { return row.entropy; },
        gray_label, "Entropy (bits/channel)", kOrange, 8.0);
    share_y_range(rgb_entropy, gray_entropy);
    write_plot_grid_pdf(
        report_directory / "compare_rgb_gray_entropy.pdf",
        "Cipher entropy: RGB888 vs grayscale",
        "identical axis limits; ideal 8 bits per 8-bit channel",
        {rgb_entropy, gray_entropy}, 2U);

    Panel rgb_correlation = scatter_mean_panel(
        rgb.rows, [](const NormalizedSweepRow& row) { return row.corr_h; },
        rgb_label, "Mean |horizontal correlation|", kBlue, 0.0);
    Panel gray_correlation = scatter_mean_panel(
        gray.rows, [](const NormalizedSweepRow& row) { return row.corr_h; },
        gray_label, "Mean |horizontal correlation|", kOrange, 0.0);
    share_y_range(rgb_correlation, gray_correlation);
    write_plot_grid_pdf(
        report_directory / "compare_rgb_gray_correlation.pdf",
        "Adjacent-pixel correlation: RGB888 vs grayscale",
        "identical axis limits; ideal zero correlation",
        {rgb_correlation, gray_correlation}, 2U);

    Panel rgb_npcr = scatter_mean_panel(
        rgb.rows, [](const NormalizedSweepRow& row) { return row.npcr; },
        rgb_label + " - NPCR", "NPCR (%)", kBlue,
        rgb.metadata.npcr_ideal);
    Panel gray_npcr = scatter_mean_panel(
        gray.rows, [](const NormalizedSweepRow& row) { return row.npcr; },
        gray_label + " - NPCR", "NPCR (%)", kOrange,
        gray.metadata.npcr_ideal);
    share_y_range(rgb_npcr, gray_npcr);
    Panel rgb_uaci = scatter_mean_panel(
        rgb.rows, [](const NormalizedSweepRow& row) { return row.uaci; },
        rgb_label + " - UACI", "UACI (%)", kBlue,
        rgb.metadata.uaci_ideal);
    Panel gray_uaci = scatter_mean_panel(
        gray.rows, [](const NormalizedSweepRow& row) { return row.uaci; },
        gray_label + " - UACI", "UACI (%)", kOrange,
        gray.metadata.uaci_ideal);
    share_y_range(rgb_uaci, gray_uaci);
    write_plot_grid_pdf(
        report_directory / "compare_rgb_gray_npcr_uaci.pdf",
        "NPCR and UACI: RGB888 vs grayscale",
        "each panel uses its own 24-bit or 8-bit ideal; shared metric limits",
        {rgb_npcr, gray_npcr, rgb_uaci, gray_uaci}, 2U);
}

void verify_matlab_fast_path(std::ostream& progress)
{
    const std::vector<std::size_t> k_values = make_k_values(24U, 24U, 384U);
    const Bytes plain{
        0U, 1U, 255U, 127U, 64U,
        200U, 17U, 42U, 99U, 128U,
        13U, 240U, 7U, 181U, 55U,
        170U, 85U, 100U, 3U, 222U,
    };

    for (const std::size_t k : k_values) {
        const TransformPlan plan = make_transform_plan(k);
        for (std::size_t input_bit = 0U; input_bit < k; ++input_bit) {
            Bits input(k, Byte{0});
            input[input_bit] = Byte{1};
            const Bits actual = transform_fast(input, plan);
            const Bits expected = transform_canonical(input);
            if (actual != expected) {
                throw std::runtime_error(
                    "fast/canonical basis mismatch at K=" +
                    std::to_string(k) + " bit=" + std::to_string(input_bit));
            }
        }

        Bits deterministic(k, Byte{0});
        constexpr std::uint64_t salt = 11U;
        for (std::size_t index = 0U; index < k; ++index) {
            const std::uint64_t i = static_cast<std::uint64_t>(index);
            const std::uint64_t value = i * (73U + salt) +
                                        (i / 3U) * 19U + 11U * salt + 5U;
            deterministic[index] = static_cast<Byte>(
                ((value >> 0U) & 1U) != ((value >> 3U) & 1U));
        }
        const std::array<std::size_t, 4> lengths{{
            1U, 7U, plain.size(), 3U * k + 5U,
        }};
        for (const std::size_t length : lengths) {
            if (keystream_fast(deterministic, length) !=
                keystream_canonical(deterministic, length)) {
                throw std::runtime_error(
                    "keystream mismatch at K=" + std::to_string(k) +
                    " for " + std::to_string(length) + " bytes");
            }
        }

        const Bits key = secret_key(k);
        const EncryptionResult fast = encrypt_fast(plain, key);
        const EncryptionResult reference = encrypt_canonical(plain, key);
        if (fast.seed != reference.seed) {
            throw std::runtime_error("seed mismatch at K=" + std::to_string(k));
        }
        if (fast.cipher.size() != plain.size()) {
            throw std::runtime_error("cipher shape mismatch at K=" +
                                     std::to_string(k));
        }
        if (fast.cipher != reference.cipher) {
            throw std::runtime_error("fast/canonical ciphertext mismatch at K=" +
                                     std::to_string(k));
        }
        if (decrypt_canonical(fast.cipher, fast.seed) != plain) {
            throw std::runtime_error("round trip failed at K=" +
                                     std::to_string(k));
        }
        const std::size_t expected = expected_iterations(plain.size(), k);
        if (fast.iterations != expected) {
            throw std::runtime_error(
                "iteration count " + std::to_string(fast.iterations) +
                " != ceil(" + std::to_string(plain.size()) + "*8/" +
                std::to_string(k) + ") at K=" + std::to_string(k));
        }
        progress << "Verified MATLAB fast-path assertions at K=" << k << '\n';
        progress.flush();
    }
    progress << "All grayscale fast-path tests passed for K=24:24:384 "
                "(fast == canonical transform, keystream, ciphertext; "
                "round trip OK).\n";
}

}  // namespace xormap_image
