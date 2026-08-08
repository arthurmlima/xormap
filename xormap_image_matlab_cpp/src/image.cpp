#include "xormap_image/image.hpp"

#include <tiffio.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace xormap_image {
namespace {

[[nodiscard]] std::size_t checked_pixel_count(std::size_t width,
                                               std::size_t height)
{
    if (width == 0 || height == 0) {
        throw std::invalid_argument("image dimensions must be positive");
    }
    if (height > std::numeric_limits<std::size_t>::max() / width) {
        throw std::length_error("image dimensions overflow size_t");
    }
    return width * height;
}

[[nodiscard]] std::string describe_path(const std::filesystem::path& path)
{
    return path.string();
}

[[noreturn]] void throw_tiff_error(const std::filesystem::path& path,
                                   const std::string& message)
{
    throw std::runtime_error("TIFF '" + describe_path(path) + "': " +
                             message);
}

struct TiffCloser {
    void operator()(TIFF* handle) const noexcept
    {
        if (handle != nullptr) {
            TIFFClose(handle);
        }
    }
};

using TiffHandle = std::unique_ptr<TIFF, TiffCloser>;

template <typename... Values>
void set_tiff_field(TIFF* handle, const std::filesystem::path& path,
                    std::uint32_t tag, Values... values)
{
    if (TIFFSetField(handle, tag, values...) != 1) {
        throw_tiff_error(path, "could not set required TIFF tag " +
                                   std::to_string(tag));
    }
}

struct Coordinates {
    std::size_t x;
    std::size_t y;
};

[[nodiscard]] Coordinates normalize_coordinates(std::size_t source_x,
                                                std::size_t source_y,
                                                std::size_t source_width,
                                                std::size_t source_height,
                                                std::uint16_t orientation)
{
    switch (orientation) {
    case ORIENTATION_TOPLEFT:
        return {source_x, source_y};
    case ORIENTATION_TOPRIGHT:
        return {source_width - 1 - source_x, source_y};
    case ORIENTATION_BOTRIGHT:
        return {source_width - 1 - source_x,
                source_height - 1 - source_y};
    case ORIENTATION_BOTLEFT:
        return {source_x, source_height - 1 - source_y};
    case ORIENTATION_LEFTTOP:
        return {source_y, source_x};
    case ORIENTATION_RIGHTTOP:
        return {source_height - 1 - source_y, source_x};
    case ORIENTATION_RIGHTBOT:
        return {source_height - 1 - source_y,
                source_width - 1 - source_x};
    case ORIENTATION_LEFTBOT:
        return {source_y, source_width - 1 - source_x};
    default:
        throw std::logic_error("unvalidated TIFF orientation");
    }
}

struct CsvRecord {
    std::vector<std::string> fields;
    std::size_t line = 0;
};

[[noreturn]] void throw_csv_error(const std::filesystem::path& path,
                                  std::size_t line,
                                  const std::string& message)
{
    throw std::runtime_error("CSV '" + describe_path(path) + "', line " +
                             std::to_string(line) + ": " + message);
}

[[nodiscard]] std::vector<CsvRecord> parse_csv(
    const std::string& input, const std::filesystem::path& path)
{
    enum class State { field_start, unquoted, quoted, quote_closed };

    std::vector<CsvRecord> records;
    std::vector<std::string> fields;
    std::string field;
    State state = State::field_start;
    std::size_t line = 1;
    std::size_t record_line = 1;
    bool record_started = false;

    const auto finish_field = [&]() {
        fields.push_back(std::move(field));
        field.clear();
        state = State::field_start;
    };
    const auto finish_record = [&]() {
        finish_field();
        records.push_back({std::move(fields), record_line});
        fields.clear();
        record_started = false;
    };

    std::size_t i = 0;
    while (i < input.size()) {
        const char ch = input[i];

        if (state == State::quoted) {
            if (ch == '"') {
                if (i + 1 < input.size() && input[i + 1] == '"') {
                    field.push_back('"');
                    i += 2;
                    continue;
                }
                state = State::quote_closed;
                ++i;
                continue;
            }
            if (ch == '\r' || ch == '\n') {
                if (ch == '\r' && i + 1 < input.size() &&
                    input[i + 1] == '\n') {
                    ++i;
                }
                field.push_back('\n');
                ++line;
                ++i;
                continue;
            }
            field.push_back(ch);
            ++i;
            continue;
        }

        if (state == State::quote_closed) {
            if (ch == ',') {
                finish_field();
                ++i;
                continue;
            }
            if (ch != '\r' && ch != '\n') {
                throw_csv_error(path, line,
                                "unexpected character after closing quote");
            }
        } else if (ch == ',') {
            record_started = true;
            finish_field();
            ++i;
            continue;
        } else if (ch == '"') {
            if (state != State::field_start) {
                throw_csv_error(path, line,
                                "quote in an unquoted CSV field");
            }
            record_started = true;
            state = State::quoted;
            ++i;
            continue;
        } else if (ch != '\r' && ch != '\n') {
            record_started = true;
            state = State::unquoted;
            field.push_back(ch);
            ++i;
            continue;
        }

        // A record delimiter reached from any non-quoted state.
        finish_record();
        if (ch == '\r' && i + 1 < input.size() && input[i + 1] == '\n') {
            ++i;
        }
        ++line;
        ++i;
        record_line = line;
    }

    if (state == State::quoted) {
        throw_csv_error(path, record_line, "unterminated quoted field");
    }
    if (record_started || !fields.empty() || !field.empty() ||
        state == State::quote_closed) {
        finish_record();
    }
    return records;
}

[[nodiscard]] bool is_blank_record(const CsvRecord& record)
{
    return record.fields.size() == 1 && record.fields.front().empty();
}

[[nodiscard]] std::string_view trim_ascii_space(std::string_view value)
{
    constexpr std::string_view whitespace = " \t\r\n";
    const std::size_t first = value.find_first_not_of(whitespace);
    if (first == std::string_view::npos) {
        return {};
    }
    const std::size_t last = value.find_last_not_of(whitespace);
    return value.substr(first, last - first + 1);
}

[[nodiscard]] std::size_t parse_positive_size(
    std::string_view text, const std::filesystem::path& path,
    std::size_t line, std::string_view column)
{
    text = trim_ascii_space(text);
    std::size_t result = 0;
    const char* const begin = text.data();
    const char* const end = begin + text.size();
    const auto parsed = std::from_chars(begin, end, result);
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != end ||
        result == 0) {
        throw_csv_error(path, line,
                        "column '" + std::string(column) +
                            "' must be a positive integer");
    }
    return result;
}

[[nodiscard]] std::filesystem::path make_absolute_normalized(
    const std::filesystem::path& path)
{
    return std::filesystem::absolute(path).lexically_normal();
}

}  // namespace

Image::Image(std::size_t width, std::size_t height)
    : width_(width),
      height_(height),
      pixels_(checked_pixel_count(width, height), std::uint8_t{0})
{
}

Image::Image(std::size_t width, std::size_t height,
             std::vector<std::uint8_t> pixels)
    : width_(width), height_(height), pixels_(std::move(pixels))
{
    const std::size_t expected = checked_pixel_count(width, height);
    if (pixels_.size() != expected) {
        throw std::invalid_argument("pixel count does not match image dimensions");
    }
}

const std::uint8_t& Image::at(std::size_t x, std::size_t y) const
{
    if (x >= width_ || y >= height_) {
        throw std::out_of_range("image pixel coordinates are out of range");
    }
    return pixels_[y * width_ + x];
}

std::uint8_t& Image::at(std::size_t x, std::size_t y)
{
    return const_cast<std::uint8_t&>(std::as_const(*this).at(x, y));
}

void Image::validate() const
{
    const std::size_t expected = checked_pixel_count(width_, height_);
    if (pixels_.size() != expected) {
        throw std::logic_error("image pixel count does not match its dimensions");
    }
}

bool operator==(const Image& lhs, const Image& rhs) noexcept
{
    return lhs.width_ == rhs.width_ && lhs.height_ == rhs.height_ &&
           lhs.pixels_ == rhs.pixels_;
}

Image load_tiff_gray8(const std::filesystem::path& path)
{
    TiffHandle handle(TIFFOpen(path.string().c_str(), "r"));
    if (!handle) {
        throw_tiff_error(path, "could not open file for reading");
    }
    TIFF* const tiff = handle.get();

    if (TIFFIsTiled(tiff) != 0) {
        throw_tiff_error(path, "tiled images are unsupported; expected scanlines");
    }
    if (TIFFLastDirectory(tiff) == 0) {
        throw_tiff_error(path,
                         "multiple image directories are unsupported; expected a 2D image");
    }

    std::uint32_t stored_width = 0;
    std::uint32_t stored_height = 0;
    if (TIFFGetField(tiff, TIFFTAG_IMAGEWIDTH, &stored_width) != 1 ||
        TIFFGetField(tiff, TIFFTAG_IMAGELENGTH, &stored_height) != 1 ||
        stored_width == 0 || stored_height == 0) {
        throw_tiff_error(path, "missing or invalid image dimensions");
    }

    std::uint32_t image_depth = 1;
    if (TIFFGetField(tiff, TIFFTAG_IMAGEDEPTH, &image_depth) == 1 &&
        image_depth != 1) {
        throw_tiff_error(path, "3D TIFF image depths are unsupported");
    }

    std::uint16_t bits_per_sample = 0;
    std::uint16_t samples_per_pixel = 0;
    std::uint16_t sample_format = 0;
    std::uint16_t planar_configuration = 0;
    std::uint16_t orientation = 0;
    std::uint16_t photometric = 0;

    if (TIFFGetFieldDefaulted(tiff, TIFFTAG_BITSPERSAMPLE,
                              &bits_per_sample) != 1 ||
        TIFFGetFieldDefaulted(tiff, TIFFTAG_SAMPLESPERPIXEL,
                              &samples_per_pixel) != 1 ||
        TIFFGetFieldDefaulted(tiff, TIFFTAG_SAMPLEFORMAT, &sample_format) !=
            1 ||
        TIFFGetFieldDefaulted(tiff, TIFFTAG_PLANARCONFIG,
                              &planar_configuration) != 1 ||
        TIFFGetFieldDefaulted(tiff, TIFFTAG_ORIENTATION, &orientation) != 1) {
        throw_tiff_error(path, "could not read required TIFF sample tags");
    }
    if (TIFFGetField(tiff, TIFFTAG_PHOTOMETRIC, &photometric) != 1) {
        throw_tiff_error(path, "missing PhotometricInterpretation tag");
    }

    if (bits_per_sample != 8) {
        throw_tiff_error(path, "expected 8 bits per sample, found " +
                                   std::to_string(bits_per_sample));
    }
    if (samples_per_pixel != 1) {
        throw_tiff_error(path, "expected one sample per pixel, found " +
                                   std::to_string(samples_per_pixel));
    }
    if (sample_format != SAMPLEFORMAT_UINT) {
        throw_tiff_error(path, "expected unsigned integer samples");
    }
    if (planar_configuration != PLANARCONFIG_CONTIG &&
        planar_configuration != PLANARCONFIG_SEPARATE) {
        throw_tiff_error(path, "unsupported planar configuration");
    }
    if (orientation < ORIENTATION_TOPLEFT ||
        orientation > ORIENTATION_LEFTBOT) {
        throw_tiff_error(path, "invalid TIFF orientation value " +
                                   std::to_string(orientation));
    }
    if (photometric != PHOTOMETRIC_MINISBLACK &&
        photometric != PHOTOMETRIC_MINISWHITE) {
        throw_tiff_error(
            path,
            "expected grayscale MINISBLACK or MINISWHITE photometric data");
    }

    const bool swaps_axes = orientation >= ORIENTATION_LEFTTOP;
    const std::size_t output_width =
        swaps_axes ? static_cast<std::size_t>(stored_height)
                   : static_cast<std::size_t>(stored_width);
    const std::size_t output_height =
        swaps_axes ? static_cast<std::size_t>(stored_width)
                   : static_cast<std::size_t>(stored_height);
    Image image(output_width, output_height);

    const tmsize_t scanline_size = TIFFScanlineSize(tiff);
    if (scanline_size <= 0 ||
        static_cast<std::uint64_t>(scanline_size) < stored_width ||
        static_cast<std::uint64_t>(scanline_size) >
            std::numeric_limits<std::size_t>::max()) {
        throw_tiff_error(path, "invalid TIFF scanline size");
    }
    std::vector<std::uint8_t> scanline(
        static_cast<std::size_t>(scanline_size));

    for (std::uint32_t source_y = 0; source_y < stored_height; ++source_y) {
        if (TIFFReadScanline(tiff, scanline.data(), source_y, 0) < 0) {
            throw_tiff_error(path, "failed while reading scanline " +
                                       std::to_string(source_y));
        }
        for (std::uint32_t source_x = 0; source_x < stored_width; ++source_x) {
            const Coordinates output = normalize_coordinates(
                source_x, source_y, stored_width, stored_height, orientation);
            const std::uint8_t sample = scanline[source_x];
            image.data()[output.y * output_width + output.x] =
                photometric == PHOTOMETRIC_MINISWHITE
                    ? static_cast<std::uint8_t>(255U - sample)
                    : sample;
        }
    }

    return image;
}

void write_tiff_gray8(const std::filesystem::path& path, const Image& image)
{
    image.validate();
    if (image.width() > std::numeric_limits<std::uint32_t>::max() ||
        image.height() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("TIFF dimensions exceed the 32-bit tag limit");
    }

    TiffHandle handle(TIFFOpen(path.string().c_str(), "w"));
    if (!handle) {
        throw_tiff_error(path, "could not open file for writing");
    }
    TIFF* const tiff = handle.get();
    const auto width = static_cast<std::uint32_t>(image.width());
    const auto height = static_cast<std::uint32_t>(image.height());

    set_tiff_field(tiff, path, TIFFTAG_IMAGEWIDTH, width);
    set_tiff_field(tiff, path, TIFFTAG_IMAGELENGTH, height);
    set_tiff_field(tiff, path, TIFFTAG_BITSPERSAMPLE,
                   static_cast<std::uint16_t>(8));
    set_tiff_field(tiff, path, TIFFTAG_SAMPLESPERPIXEL,
                   static_cast<std::uint16_t>(1));
    set_tiff_field(tiff, path, TIFFTAG_SAMPLEFORMAT,
                   static_cast<std::uint16_t>(SAMPLEFORMAT_UINT));
    set_tiff_field(tiff, path, TIFFTAG_PHOTOMETRIC,
                   static_cast<std::uint16_t>(PHOTOMETRIC_MINISBLACK));
    set_tiff_field(tiff, path, TIFFTAG_PLANARCONFIG,
                   static_cast<std::uint16_t>(PLANARCONFIG_CONTIG));
    set_tiff_field(tiff, path, TIFFTAG_ORIENTATION,
                   static_cast<std::uint16_t>(ORIENTATION_TOPLEFT));
    set_tiff_field(tiff, path, TIFFTAG_COMPRESSION,
                   static_cast<std::uint16_t>(COMPRESSION_NONE));

    std::uint32_t rows_per_strip = TIFFDefaultStripSize(tiff, 0);
    if (rows_per_strip == 0) {
        rows_per_strip = 1;
    }
    set_tiff_field(tiff, path, TIFFTAG_ROWSPERSTRIP, rows_per_strip);

    for (std::uint32_t y = 0; y < height; ++y) {
        auto* const row = const_cast<std::uint8_t*>(
            image.data() + static_cast<std::size_t>(y) * image.width());
        if (TIFFWriteScanline(tiff, row, y, 0) < 0) {
            throw_tiff_error(path, "failed while writing scanline " +
                                       std::to_string(y));
        }
    }
    if (TIFFWriteDirectory(tiff) != 1) {
        throw_tiff_error(path, "could not finalize TIFF directory");
    }
}

std::vector<ManifestEntry> read_gray_manifest(
    const std::filesystem::path& manifest_path,
    const std::filesystem::path& image_directory)
{
    std::ifstream input(manifest_path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("could not open grayscale manifest '" +
                                 describe_path(manifest_path) + "'");
    }
    std::ostringstream contents;
    contents << input.rdbuf();
    if (input.bad()) {
        throw std::runtime_error("failed while reading grayscale manifest '" +
                                 describe_path(manifest_path) + "'");
    }

    std::vector<CsvRecord> records = parse_csv(contents.str(), manifest_path);
    records.erase(std::remove_if(records.begin(), records.end(), is_blank_record),
                  records.end());
    if (records.empty()) {
        throw_csv_error(manifest_path, 1, "manifest is empty");
    }

    if (!records.front().fields.empty() &&
        records.front().fields.front().size() >= 3) {
        std::string& first = records.front().fields.front();
        const auto byte = [](char value) {
            return static_cast<unsigned char>(value);
        };
        if (byte(first[0]) == 0xEFU && byte(first[1]) == 0xBBU &&
            byte(first[2]) == 0xBFU) {
            first.erase(0, 3);
        }
    }

    constexpr std::array<std::string_view, 6> required_columns = {
        "filename", "volume", "name", "height", "width", "channels"};
    const CsvRecord& header = records.front();
    if (header.fields.size() != required_columns.size()) {
        throw_csv_error(manifest_path, header.line,
                        "header must contain exactly filename, volume, name, "
                        "height, width, channels");
    }
    std::array<std::size_t, required_columns.size()> indexes{};
    for (std::size_t required = 0; required < required_columns.size();
         ++required) {
        const auto found = std::find(header.fields.begin(), header.fields.end(),
                                     required_columns[required]);
        if (found == header.fields.end()) {
            throw_csv_error(manifest_path, header.line,
                            "missing required column '" +
                                std::string(required_columns[required]) + "'");
        }
        indexes[required] =
            static_cast<std::size_t>(std::distance(header.fields.begin(), found));
    }

    const std::filesystem::path base_directory = image_directory.empty()
        ? make_absolute_normalized(manifest_path).parent_path()
        : make_absolute_normalized(image_directory);

    std::vector<ManifestEntry> entries;
    entries.reserve(records.size() - 1);
    for (std::size_t row = 1; row < records.size(); ++row) {
        const CsvRecord& record = records[row];
        if (record.fields.size() != required_columns.size()) {
            throw_csv_error(manifest_path, record.line,
                            "expected exactly 6 fields, found " +
                                std::to_string(record.fields.size()));
        }

        ManifestEntry entry;
        entry.filename = record.fields[indexes[0]];
        entry.volume = record.fields[indexes[1]];
        entry.name = record.fields[indexes[2]];
        entry.height = parse_positive_size(record.fields[indexes[3]],
                                           manifest_path, record.line, "height");
        entry.width = parse_positive_size(record.fields[indexes[4]], manifest_path,
                                          record.line, "width");
        entry.channels = parse_positive_size(record.fields[indexes[5]],
                                             manifest_path, record.line,
                                             "channels");
        if (entry.filename.empty()) {
            throw_csv_error(manifest_path, record.line,
                            "filename must not be empty");
        }
        if (entry.volume.empty()) {
            throw_csv_error(manifest_path, record.line,
                            "volume must not be empty");
        }
        if (entry.name.empty()) {
            throw_csv_error(manifest_path, record.line,
                            "name must not be empty");
        }
        if (entry.channels != 1) {
            throw_csv_error(manifest_path, record.line,
                            "channels must be 1 for a grayscale manifest");
        }
        if (entry.height >
            std::numeric_limits<std::size_t>::max() / entry.width) {
            throw_csv_error(manifest_path, record.line,
                            "image dimensions overflow size_t");
        }

        const std::filesystem::path filename(entry.filename);
        entry.path = filename.is_absolute()
            ? filename.lexically_normal()
            : (base_directory / filename).lexically_normal();
        entries.push_back(std::move(entry));
    }
    return entries;
}

std::string csv_escape(std::string_view field)
{
    if (field.find_first_of(",\"\r\n") == std::string_view::npos) {
        return std::string(field);
    }

    std::string escaped;
    escaped.reserve(field.size() + 2);
    escaped.push_back('"');
    for (const char ch : field) {
        if (ch == '"') {
            escaped.push_back('"');
        }
        escaped.push_back(ch);
    }
    escaped.push_back('"');
    return escaped;
}

}  // namespace xormap_image
