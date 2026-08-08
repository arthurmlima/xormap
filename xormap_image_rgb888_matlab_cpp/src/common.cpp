#include "xormap_color/common.hpp"

#include <cairo-pdf.h>
#include <cairo.h>
#include <tiffio.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace xormap_color {
namespace {

constexpr std::array<char, 8> kRgb565Magic{{'R', 'G', 'B', '5', '6', '5', 'B', 'E'}};

[[nodiscard]] std::size_t checked_pixels(std::size_t width, std::size_t height)
{
    if (width == 0 || height == 0) {
        throw std::invalid_argument("image dimensions must be positive");
    }
    if (height > std::numeric_limits<std::size_t>::max() / width) {
        throw std::length_error("image dimensions overflow size_t");
    }
    return width * height;
}

[[nodiscard]] Word word_mask(unsigned int word_bits)
{
    if (word_bits == 0U || word_bits > 32U || word_bits % 8U != 0U) {
        throw std::invalid_argument(
            "word_bits must be a nonzero multiple of 8 no greater than 32");
    }
    return word_bits == 32U
        ? std::numeric_limits<Word>::max()
        : static_cast<Word>((Word{1} << word_bits) - Word{1});
}

void validate_words(const Words& words, unsigned int word_bits)
{
    const Word mask = word_mask(word_bits);
    for (const Word word : words) {
        if ((word & ~mask) != 0U) {
            throw std::invalid_argument("packed word exceeds its declared bit width");
        }
    }
}

[[nodiscard]] Words bytes_to_words_le(const xormap_image::Bytes& bytes,
                                      unsigned int word_bits)
{
    const std::size_t bytes_per_word = word_bits / 8U;
    if (bytes.size() % bytes_per_word != 0U) {
        throw std::logic_error("keystream byte count is not word aligned");
    }
    Words words(bytes.size() / bytes_per_word, Word{0});
    for (std::size_t i = 0; i < words.size(); ++i) {
        Word value = 0;
        for (std::size_t byte = 0; byte < bytes_per_word; ++byte) {
            value |= static_cast<Word>(bytes[i * bytes_per_word + byte])
                     << static_cast<unsigned int>(8U * byte);
        }
        words[i] = value;
    }
    return words;
}

[[nodiscard]] Words xor_words(const Words& first, const Words& second)
{
    if (first.size() != second.size()) {
        throw std::invalid_argument("word XOR operands must have equal lengths");
    }
    Words result(first.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        result[i] = first[i] ^ second[i];
    }
    return result;
}

[[nodiscard]] xormap_image::Bits derive_seed(
    const Words& plain, unsigned int word_bits,
    const xormap_image::Bits& key_bits)
{
    xormap_image::validate_bits(key_bits, "key_bits");
    if (key_bits.size() <= 4U) {
        throw std::invalid_argument("K must be greater than 4");
    }
    validate_words(plain, word_bits);
    xormap_image::Bits seed = xormap_image::hash_expand_bits(
        words_to_bytes_be(plain, word_bits), key_bits.size());
    for (std::size_t i = 0; i < seed.size(); ++i) {
        seed[i] ^= key_bits[i];
    }
    return seed;
}

[[nodiscard]] std::size_t iteration_count(std::size_t num_words,
                                          unsigned int word_bits,
                                          std::size_t k)
{
    if (num_words > std::numeric_limits<std::size_t>::max() / word_bits) {
        throw std::length_error("word stream is too large");
    }
    const std::size_t bits = num_words * word_bits;
    return bits / k + (bits % k != 0U ? 1U : 0U);
}

struct TiffCloser {
    void operator()(TIFF* value) const noexcept
    {
        if (value != nullptr) {
            TIFFClose(value);
        }
    }
};
using TiffHandle = std::unique_ptr<TIFF, TiffCloser>;

[[noreturn]] void tiff_error(const std::filesystem::path& path,
                             const std::string& message)
{
    throw std::runtime_error("TIFF '" + path.string() + "': " + message);
}

struct Coordinates { std::size_t x; std::size_t y; };

[[nodiscard]] Coordinates orient(std::size_t x, std::size_t y,
                                 std::size_t width, std::size_t height,
                                 std::uint16_t orientation)
{
    switch (orientation) {
    case ORIENTATION_TOPLEFT: return {x, y};
    case ORIENTATION_TOPRIGHT: return {width - 1U - x, y};
    case ORIENTATION_BOTRIGHT: return {width - 1U - x, height - 1U - y};
    case ORIENTATION_BOTLEFT: return {x, height - 1U - y};
    case ORIENTATION_LEFTTOP: return {y, x};
    case ORIENTATION_RIGHTTOP: return {height - 1U - y, x};
    case ORIENTATION_RIGHTBOT: return {height - 1U - y, width - 1U - x};
    case ORIENTATION_LEFTBOT: return {y, width - 1U - x};
    default: throw std::logic_error("unvalidated TIFF orientation");
    }
}

template <typename... Values>
void set_field(TIFF* tiff, const std::filesystem::path& path,
               std::uint32_t tag, Values... values)
{
    if (TIFFSetField(tiff, tag, values...) != 1) {
        tiff_error(path, "could not set required tag " + std::to_string(tag));
    }
}

[[nodiscard]] std::vector<std::string> parse_csv_line(std::string_view line)
{
    std::vector<std::string> fields;
    std::string field;
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (quoted) {
            if (ch == '"') {
                if (i + 1U < line.size() && line[i + 1U] == '"') {
                    field.push_back('"');
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                field.push_back(ch);
            }
        } else if (ch == ',') {
            fields.push_back(std::move(field));
            field.clear();
        } else if (ch == '"' && field.empty()) {
            quoted = true;
        } else {
            field.push_back(ch);
        }
    }
    if (quoted) {
        throw std::runtime_error("unterminated quoted CSV field");
    }
    fields.push_back(std::move(field));
    return fields;
}

void write_be32(std::ostream& output, std::uint32_t value)
{
    const std::array<char, 4> bytes{{
        static_cast<char>((value >> 24U) & 0xFFU),
        static_cast<char>((value >> 16U) & 0xFFU),
        static_cast<char>((value >> 8U) & 0xFFU),
        static_cast<char>(value & 0xFFU)}};
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

[[nodiscard]] std::uint32_t read_be32(std::istream& input)
{
    std::array<unsigned char, 4> bytes{};
    input.read(reinterpret_cast<char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    if (!input) {
        throw std::runtime_error("truncated RGB565 header");
    }
    return (static_cast<std::uint32_t>(bytes[0]) << 24U) |
           (static_cast<std::uint32_t>(bytes[1]) << 16U) |
           (static_cast<std::uint32_t>(bytes[2]) << 8U) |
           static_cast<std::uint32_t>(bytes[3]);
}

void check_cairo(cairo_status_t status, const std::string& context)
{
    if (status != CAIRO_STATUS_SUCCESS) {
        throw std::runtime_error(context + ": " + cairo_status_to_string(status));
    }
}

void centered_text(cairo_t* cr, double center_x, double y,
                   const std::string& value, double font_size, bool bold)
{
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                           bold ? CAIRO_FONT_WEIGHT_BOLD
                                : CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, font_size);
    cairo_text_extents_t extents{};
    cairo_text_extents(cr, value.c_str(), &extents);
    cairo_move_to(cr, center_x - (extents.width / 2.0 + extents.x_bearing), y);
    cairo_show_text(cr, value.c_str());
}

void draw_rgb_panel(cairo_t* cr, double x, double y, double width, double height,
                    const std::string& label, const RgbImage& image)
{
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_rectangle(cr, x, y, width, height);
    cairo_fill_preserve(cr);
    cairo_set_source_rgb(cr, 0.82, 0.84, 0.86);
    cairo_set_line_width(cr, 0.8);
    cairo_stroke(cr);

    cairo_set_source_rgb(cr, 0.08, 0.10, 0.12);
    centered_text(cr, x + width / 2.0, y + 28.0, label, 13.0, false);

    std::vector<std::uint32_t> argb(image.size());
    for (std::size_t i = 0; i < image.size(); ++i) {
        argb[i] = 0xFF000000U |
                  (static_cast<std::uint32_t>(image.pixels[i * 3U]) << 16U) |
                  (static_cast<std::uint32_t>(image.pixels[i * 3U + 1U]) << 8U) |
                  static_cast<std::uint32_t>(image.pixels[i * 3U + 2U]);
    }
    cairo_surface_t* surface = cairo_image_surface_create_for_data(
        reinterpret_cast<unsigned char*>(argb.data()), CAIRO_FORMAT_ARGB32,
        static_cast<int>(image.width), static_cast<int>(image.height),
        static_cast<int>(image.width * sizeof(std::uint32_t)));
    check_cairo(cairo_surface_status(surface), "could not create RGB image surface");

    const double available_w = width - 42.0;
    const double available_h = height - 72.0;
    const double scale = std::min(available_w / static_cast<double>(image.width),
                                  available_h / static_cast<double>(image.height));
    const double draw_w = static_cast<double>(image.width) * scale;
    const double draw_h = static_cast<double>(image.height) * scale;
    const double draw_x = x + (width - draw_w) / 2.0;
    const double draw_y = y + 38.0 + (available_h - draw_h) / 2.0;

    cairo_save(cr);
    cairo_translate(cr, draw_x, draw_y);
    cairo_scale(cr, scale, scale);
    cairo_set_source_surface(cr, surface, 0.0, 0.0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
    cairo_rectangle(cr, 0.0, 0.0, static_cast<double>(image.width),
                    static_cast<double>(image.height));
    cairo_fill(cr);
    cairo_restore(cr);
    cairo_surface_destroy(surface);

    cairo_set_source_rgb(cr, 0.32, 0.35, 0.38);
    centered_text(cr, x + width / 2.0, y + height - 13.0,
                  std::to_string(image.width) + " x " +
                      std::to_string(image.height) + " pixels",
                  8.5, false);
}

}  // namespace

RgbImage::RgbImage(std::size_t image_width, std::size_t image_height,
                   std::vector<std::uint8_t> interleaved_rgb)
    : width(image_width), height(image_height), pixels(std::move(interleaved_rgb))
{
    validate();
}

void RgbImage::validate() const
{
    const std::size_t count = checked_pixels(width, height);
    if (count > std::numeric_limits<std::size_t>::max() / 3U ||
        pixels.size() != count * 3U) {
        throw std::invalid_argument("RGB byte count does not match image dimensions");
    }
}

bool operator==(const RgbImage& lhs, const RgbImage& rhs) noexcept
{
    return lhs.width == rhs.width && lhs.height == rhs.height &&
           lhs.pixels == rhs.pixels;
}

RgbImage load_tiff_rgb8(const std::filesystem::path& path)
{
    TiffHandle handle(TIFFOpen(path.string().c_str(), "r"));
    if (!handle) {
        tiff_error(path, "could not open for reading");
    }
    TIFF* const tiff = handle.get();
    if (TIFFIsTiled(tiff) != 0 || TIFFLastDirectory(tiff) == 0) {
        tiff_error(path, "expected one scanline-based 2D image");
    }

    std::uint32_t stored_width = 0;
    std::uint32_t stored_height = 0;
    std::uint16_t bits = 0;
    std::uint16_t samples = 0;
    std::uint16_t sample_format = 0;
    std::uint16_t planar = 0;
    std::uint16_t orientation = 0;
    std::uint16_t photometric = 0;
    if (TIFFGetField(tiff, TIFFTAG_IMAGEWIDTH, &stored_width) != 1 ||
        TIFFGetField(tiff, TIFFTAG_IMAGELENGTH, &stored_height) != 1 ||
        TIFFGetFieldDefaulted(tiff, TIFFTAG_BITSPERSAMPLE, &bits) != 1 ||
        TIFFGetFieldDefaulted(tiff, TIFFTAG_SAMPLESPERPIXEL, &samples) != 1 ||
        TIFFGetFieldDefaulted(tiff, TIFFTAG_SAMPLEFORMAT, &sample_format) != 1 ||
        TIFFGetFieldDefaulted(tiff, TIFFTAG_PLANARCONFIG, &planar) != 1 ||
        TIFFGetFieldDefaulted(tiff, TIFFTAG_ORIENTATION, &orientation) != 1 ||
        TIFFGetField(tiff, TIFFTAG_PHOTOMETRIC, &photometric) != 1) {
        tiff_error(path, "missing required image tags");
    }
    if (stored_width == 0U || stored_height == 0U || bits != 8U ||
        samples != 3U || sample_format != SAMPLEFORMAT_UINT ||
        photometric != PHOTOMETRIC_RGB ||
        (planar != PLANARCONFIG_CONTIG && planar != PLANARCONFIG_SEPARATE) ||
        orientation < ORIENTATION_TOPLEFT || orientation > ORIENTATION_LEFTBOT) {
        tiff_error(path, "expected unsigned 8-bit three-channel RGB data");
    }

    const bool swap = orientation >= ORIENTATION_LEFTTOP;
    const std::size_t width = swap ? stored_height : stored_width;
    const std::size_t height = swap ? stored_width : stored_height;
    std::vector<std::uint8_t> pixels(checked_pixels(width, height) * 3U);
    const tmsize_t scanline_size = TIFFScanlineSize(tiff);
    const std::size_t minimum = static_cast<std::size_t>(stored_width) *
                                (planar == PLANARCONFIG_CONTIG ? 3U : 1U);
    if (scanline_size <= 0 || static_cast<std::size_t>(scanline_size) < minimum) {
        tiff_error(path, "invalid scanline size");
    }
    std::vector<std::uint8_t> scanline(static_cast<std::size_t>(scanline_size));

    for (std::uint32_t source_y = 0; source_y < stored_height; ++source_y) {
        if (planar == PLANARCONFIG_CONTIG) {
            if (TIFFReadScanline(tiff, scanline.data(), source_y, 0) < 0) {
                tiff_error(path, "failed reading RGB scanline");
            }
            for (std::uint32_t source_x = 0; source_x < stored_width; ++source_x) {
                const Coordinates out = orient(source_x, source_y, stored_width,
                                               stored_height, orientation);
                const std::size_t dst = (out.y * width + out.x) * 3U;
                const std::size_t src = static_cast<std::size_t>(source_x) * 3U;
                std::copy_n(scanline.data() + src, 3U, pixels.data() + dst);
            }
        } else {
            for (std::uint16_t channel = 0; channel < 3U; ++channel) {
                if (TIFFReadScanline(tiff, scanline.data(), source_y, channel) < 0) {
                    tiff_error(path, "failed reading planar RGB scanline");
                }
                for (std::uint32_t source_x = 0; source_x < stored_width; ++source_x) {
                    const Coordinates out = orient(source_x, source_y, stored_width,
                                                   stored_height, orientation);
                    pixels[(out.y * width + out.x) * 3U + channel] = scanline[source_x];
                }
            }
        }
    }
    return RgbImage(width, height, std::move(pixels));
}

void write_tiff_rgb8(const std::filesystem::path& path, const RgbImage& image)
{
    image.validate();
    if (image.width > std::numeric_limits<std::uint32_t>::max() ||
        image.height > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("TIFF dimensions exceed 32-bit tags");
    }
    TiffHandle handle(TIFFOpen(path.string().c_str(), "w"));
    if (!handle) {
        tiff_error(path, "could not open for writing");
    }
    TIFF* const tiff = handle.get();
    set_field(tiff, path, TIFFTAG_IMAGEWIDTH,
              static_cast<std::uint32_t>(image.width));
    set_field(tiff, path, TIFFTAG_IMAGELENGTH,
              static_cast<std::uint32_t>(image.height));
    set_field(tiff, path, TIFFTAG_BITSPERSAMPLE, static_cast<std::uint16_t>(8));
    set_field(tiff, path, TIFFTAG_SAMPLESPERPIXEL, static_cast<std::uint16_t>(3));
    set_field(tiff, path, TIFFTAG_SAMPLEFORMAT,
              static_cast<std::uint16_t>(SAMPLEFORMAT_UINT));
    set_field(tiff, path, TIFFTAG_PHOTOMETRIC,
              static_cast<std::uint16_t>(PHOTOMETRIC_RGB));
    set_field(tiff, path, TIFFTAG_PLANARCONFIG,
              static_cast<std::uint16_t>(PLANARCONFIG_CONTIG));
    set_field(tiff, path, TIFFTAG_ORIENTATION,
              static_cast<std::uint16_t>(ORIENTATION_TOPLEFT));
    set_field(tiff, path, TIFFTAG_COMPRESSION,
              static_cast<std::uint16_t>(COMPRESSION_NONE));
    set_field(tiff, path, TIFFTAG_ROWSPERSTRIP, TIFFDefaultStripSize(tiff, 0));
    for (std::uint32_t y = 0; y < image.height; ++y) {
        auto* row = const_cast<std::uint8_t*>(
            image.pixels.data() + static_cast<std::size_t>(y) * image.width * 3U);
        if (TIFFWriteScanline(tiff, row, y, 0) < 0) {
            tiff_error(path, "failed writing scanline");
        }
    }
    if (TIFFWriteDirectory(tiff) != 1) {
        tiff_error(path, "could not finalize image directory");
    }
}

std::vector<ColorManifestEntry> read_color_manifest(
    const std::filesystem::path& manifest_path,
    const std::filesystem::path& image_directory)
{
    std::ifstream input(manifest_path);
    if (!input) {
        throw std::runtime_error("could not open color manifest '" +
                                 manifest_path.string() + "'");
    }
    std::string line;
    if (!std::getline(input, line)) {
        throw std::runtime_error("color manifest is empty");
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    const auto header = parse_csv_line(line);
    const auto find_column = [&](const std::string& name) {
        const auto found = std::find(header.begin(), header.end(), name);
        if (found == header.end()) {
            throw std::runtime_error("color manifest is missing column '" + name + "'");
        }
        return static_cast<std::size_t>(std::distance(header.begin(), found));
    };
    const std::size_t filename_col = find_column("filename");
    const std::size_t volume_col = find_column("volume");
    const std::size_t name_col = find_column("name");
    const std::size_t max_col = std::max({filename_col, volume_col, name_col});
    const std::filesystem::path base = image_directory.empty()
        ? std::filesystem::absolute(manifest_path).parent_path()
        : std::filesystem::absolute(image_directory);

    std::vector<ColorManifestEntry> entries;
    std::size_t line_number = 1;
    while (std::getline(input, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        const auto fields = parse_csv_line(line);
        if (fields.size() <= max_col || fields[filename_col].empty() ||
            fields[volume_col].empty() || fields[name_col].empty()) {
            throw std::runtime_error("malformed color manifest row " +
                                     std::to_string(line_number));
        }
        const std::filesystem::path filename(fields[filename_col]);
        entries.push_back({fields[filename_col], fields[volume_col],
                           fields[name_col],
                           filename.is_absolute() ? filename
                               : (base / filename).lexically_normal()});
    }
    if (entries.empty()) {
        throw std::runtime_error("color manifest contains no images");
    }
    return entries;
}

Words pack_rgb888(const RgbImage& image)
{
    image.validate();
    Words words(image.size());
    for (std::size_t i = 0; i < image.size(); ++i) {
        words[i] = (static_cast<Word>(image.pixels[i * 3U]) << 16U) |
                   (static_cast<Word>(image.pixels[i * 3U + 1U]) << 8U) |
                   static_cast<Word>(image.pixels[i * 3U + 2U]);
    }
    return words;
}

RgbImage unpack_rgb888(const Words& words, std::size_t width, std::size_t height)
{
    if (words.size() != checked_pixels(width, height)) {
        throw std::invalid_argument("RGB888 word count does not match dimensions");
    }
    validate_words(words, 24U);
    std::vector<std::uint8_t> pixels(words.size() * 3U);
    for (std::size_t i = 0; i < words.size(); ++i) {
        pixels[i * 3U] = static_cast<std::uint8_t>(words[i] >> 16U);
        pixels[i * 3U + 1U] = static_cast<std::uint8_t>((words[i] >> 8U) & 0xFFU);
        pixels[i * 3U + 2U] = static_cast<std::uint8_t>(words[i] & 0xFFU);
    }
    return RgbImage(width, height, std::move(pixels));
}

Words pack_rgb565(const RgbImage& image)
{
    image.validate();
    Words words(image.size());
    for (std::size_t i = 0; i < image.size(); ++i) {
        const Word r = static_cast<Word>(image.pixels[i * 3U]) >> 3U;
        const Word g = static_cast<Word>(image.pixels[i * 3U + 1U]) >> 2U;
        const Word b = static_cast<Word>(image.pixels[i * 3U + 2U]) >> 3U;
        words[i] = (r << 11U) | (g << 5U) | b;
    }
    return words;
}

RgbImage preview_rgb565(const Words& words, std::size_t width, std::size_t height)
{
    if (words.size() != checked_pixels(width, height)) {
        throw std::invalid_argument("RGB565 word count does not match dimensions");
    }
    validate_words(words, 16U);
    std::vector<std::uint8_t> pixels(words.size() * 3U);
    for (std::size_t i = 0; i < words.size(); ++i) {
        const std::uint8_t r5 = static_cast<std::uint8_t>(words[i] >> 11U);
        const std::uint8_t g6 = static_cast<std::uint8_t>((words[i] >> 5U) & 0x3FU);
        const std::uint8_t b5 = static_cast<std::uint8_t>(words[i] & 0x1FU);
        pixels[i * 3U] = static_cast<std::uint8_t>((r5 << 3U) | (r5 >> 2U));
        pixels[i * 3U + 1U] = static_cast<std::uint8_t>((g6 << 2U) | (g6 >> 4U));
        pixels[i * 3U + 2U] = static_cast<std::uint8_t>((b5 << 3U) | (b5 >> 2U));
    }
    return RgbImage(width, height, std::move(pixels));
}

std::array<std::vector<std::uint8_t>, 3> rgb888_channels(const Words& words)
{
    validate_words(words, 24U);
    std::array<std::vector<std::uint8_t>, 3> channels;
    for (auto& channel : channels) {
        channel.resize(words.size());
    }
    for (std::size_t i = 0; i < words.size(); ++i) {
        channels[0][i] = static_cast<std::uint8_t>(words[i] >> 16U);
        channels[1][i] = static_cast<std::uint8_t>((words[i] >> 8U) & 0xFFU);
        channels[2][i] = static_cast<std::uint8_t>(words[i] & 0xFFU);
    }
    return channels;
}

std::array<std::vector<std::uint8_t>, 3> rgb565_channels(const Words& words)
{
    validate_words(words, 16U);
    std::array<std::vector<std::uint8_t>, 3> channels;
    for (auto& channel : channels) {
        channel.resize(words.size());
    }
    for (std::size_t i = 0; i < words.size(); ++i) {
        channels[0][i] = static_cast<std::uint8_t>(words[i] >> 11U);
        channels[1][i] = static_cast<std::uint8_t>((words[i] >> 5U) & 0x3FU);
        channels[2][i] = static_cast<std::uint8_t>(words[i] & 0x1FU);
    }
    return channels;
}

xormap_image::Bytes words_to_bytes_be(const Words& words, unsigned int word_bits)
{
    validate_words(words, word_bits);
    const std::size_t bytes_per_word = word_bits / 8U;
    if (words.size() > std::numeric_limits<std::size_t>::max() / bytes_per_word) {
        throw std::length_error("word vector is too large to serialize");
    }
    xormap_image::Bytes bytes(words.size() * bytes_per_word);
    for (std::size_t i = 0; i < words.size(); ++i) {
        for (std::size_t byte = 0; byte < bytes_per_word; ++byte) {
            const unsigned int shift = static_cast<unsigned int>(
                8U * (bytes_per_word - 1U - byte));
            bytes[i * bytes_per_word + byte] =
                static_cast<std::uint8_t>((words[i] >> shift) & 0xFFU);
        }
    }
    return bytes;
}

Words keystream_words_fast(const xormap_image::Bits& seed,
                           unsigned int word_bits, std::size_t num_words)
{
    const std::size_t bytes_per_word = word_bits / 8U;
    (void)word_mask(word_bits);
    if (num_words > std::numeric_limits<std::size_t>::max() / bytes_per_word) {
        throw std::length_error("keystream word count is too large");
    }
    return bytes_to_words_le(
        xormap_image::keystream_fast(seed, num_words * bytes_per_word), word_bits);
}

Words keystream_words_canonical(const xormap_image::Bits& seed,
                                unsigned int word_bits, std::size_t num_words)
{
    const std::size_t bytes_per_word = word_bits / 8U;
    (void)word_mask(word_bits);
    if (num_words > std::numeric_limits<std::size_t>::max() / bytes_per_word) {
        throw std::length_error("keystream word count is too large");
    }
    return bytes_to_words_le(
        xormap_image::keystream_canonical(seed, num_words * bytes_per_word), word_bits);
}

WordEncryptionResult encrypt_words_fast(const Words& plain,
                                        unsigned int word_bits,
                                        const xormap_image::Bits& key_bits)
{
    WordEncryptionResult result;
    result.seed = derive_seed(plain, word_bits, key_bits);
    result.cipher = xor_words(
        plain, keystream_words_fast(result.seed, word_bits, plain.size()));
    result.iterations = iteration_count(plain.size(), word_bits, key_bits.size());
    return result;
}

WordEncryptionResult encrypt_words_canonical(const Words& plain,
                                             unsigned int word_bits,
                                             const xormap_image::Bits& key_bits)
{
    WordEncryptionResult result;
    result.seed = derive_seed(plain, word_bits, key_bits);
    result.cipher = xor_words(
        plain, keystream_words_canonical(result.seed, word_bits, plain.size()));
    result.iterations = iteration_count(plain.size(), word_bits, key_bits.size());
    return result;
}

Words decrypt_words_fast(const Words& cipher, unsigned int word_bits,
                         const xormap_image::Bits& seed)
{
    validate_words(cipher, word_bits);
    return xor_words(cipher,
                     keystream_words_fast(seed, word_bits, cipher.size()));
}

xormap_image::NpcrUaciResult npcr_uaci_words(
    const Words& first, const Words& second, Word max_value)
{
    if (first.empty() || first.size() != second.size() || max_value == 0U) {
        throw std::invalid_argument("NPCR/UACI word inputs must be nonempty and equal");
    }
    std::size_t changed = 0;
    long double difference = 0.0L;
    for (std::size_t i = 0; i < first.size(); ++i) {
        changed += first[i] != second[i] ? 1U : 0U;
        difference += std::fabs(static_cast<long double>(first[i]) -
                                static_cast<long double>(second[i]));
    }
    const long double count = static_cast<long double>(first.size());
    return {100.0 * static_cast<double>(changed) / static_cast<double>(first.size()),
            static_cast<double>(100.0L * difference /
                                (count * static_cast<long double>(max_value)))};
}

xormap_image::PsnrResult psnr_words(const Words& first, const Words& second,
                                    double max_value)
{
    if (first.empty() || first.size() != second.size() || max_value <= 0.0) {
        throw std::invalid_argument("PSNR word inputs must be nonempty and equal");
    }
    long double sum = 0.0L;
    for (std::size_t i = 0; i < first.size(); ++i) {
        const long double d = static_cast<long double>(first[i]) - second[i];
        sum += d * d;
    }
    const double mse = static_cast<double>(
        sum / static_cast<long double>(first.size()));
    return {mse == 0.0 ? std::numeric_limits<double>::infinity()
                       : 10.0 * std::log10(max_value * max_value / mse),
            mse};
}

std::uint32_t diff_checksum_words(const Words& difference)
{
    constexpr std::uint64_t modulus = UINT64_C(1) << 31U;
    const std::size_t count = std::min<std::size_t>(difference.size(), 4096U);
    std::uint64_t sum = 0;
    for (std::size_t i = 0; i < count; ++i) {
        sum = (sum + (static_cast<std::uint64_t>(difference[i]) % modulus) *
                         static_cast<std::uint64_t>(i + 1U)) % modulus;
    }
    return static_cast<std::uint32_t>(sum);
}

std::size_t first_differing_word(const Words& difference)
{
    const auto found = std::find_if(difference.begin(), difference.end(),
                                    [](Word word) { return word != 0U; });
    return found == difference.end()
        ? 0U
        : static_cast<std::size_t>(std::distance(difference.begin(), found)) + 1U;
}

void write_rgb565_raw(const std::filesystem::path& path, const Words& words,
                      std::size_t width, std::size_t height)
{
    if (words.size() != checked_pixels(width, height)) {
        throw std::invalid_argument("RGB565 word count does not match dimensions");
    }
    validate_words(words, 16U);
    if (width > std::numeric_limits<std::uint32_t>::max() ||
        height > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("RGB565 dimensions exceed file format limits");
    }
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("could not create RGB565 file '" + path.string() + "'");
    }
    output.write(kRgb565Magic.data(), static_cast<std::streamsize>(kRgb565Magic.size()));
    write_be32(output, static_cast<std::uint32_t>(width));
    write_be32(output, static_cast<std::uint32_t>(height));
    for (const Word word : words) {
        output.put(static_cast<char>((word >> 8U) & 0xFFU));
        output.put(static_cast<char>(word & 0xFFU));
    }
    if (!output) {
        throw std::runtime_error("failed writing RGB565 file '" + path.string() + "'");
    }
}

Words read_rgb565_raw(const std::filesystem::path& path,
                      std::size_t& width, std::size_t& height)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("could not open RGB565 file '" + path.string() + "'");
    }
    std::array<char, 8> magic{};
    input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (!input || magic != kRgb565Magic) {
        throw std::runtime_error("invalid RGB565 file header in '" + path.string() + "'");
    }
    width = read_be32(input);
    height = read_be32(input);
    const std::size_t count = checked_pixels(width, height);
    Words words(count);
    for (Word& word : words) {
        const int hi = input.get();
        const int lo = input.get();
        if (hi == std::char_traits<char>::eof() || lo == std::char_traits<char>::eof()) {
            throw std::runtime_error("truncated RGB565 pixel data in '" + path.string() + "'");
        }
        word = (static_cast<Word>(static_cast<unsigned char>(hi)) << 8U) |
               static_cast<Word>(static_cast<unsigned char>(lo));
    }
    if (input.get() != std::char_traits<char>::eof()) {
        throw std::runtime_error("trailing bytes in RGB565 file '" + path.string() + "'");
    }
    return words;
}

void write_rgb_comparison_pdf(const std::filesystem::path& path,
                              const std::string& title,
                              const std::string& subtitle,
                              const RgbImage& plain,
                              const RgbImage& cipher,
                              const std::string& plain_label,
                              const std::string& cipher_label)
{
    plain.validate();
    cipher.validate();
    if (plain.width != cipher.width || plain.height != cipher.height) {
        throw std::invalid_argument("RGB comparison images must have equal dimensions");
    }
    std::filesystem::create_directories(path.parent_path());
    cairo_surface_t* surface = cairo_pdf_surface_create(path.string().c_str(), 906.0, 432.0);
    check_cairo(cairo_surface_status(surface), "could not create RGB PDF");
    cairo_t* cr = cairo_create(surface);
    check_cairo(cairo_status(cr), "could not create Cairo context");
    cairo_set_source_rgb(cr, 0.975, 0.98, 0.985);
    cairo_paint(cr);
    cairo_set_source_rgb(cr, 0.08, 0.10, 0.12);
    centered_text(cr, 453.0, 38.0, title, 19.0, false);
    cairo_set_source_rgb(cr, 0.32, 0.35, 0.38);
    centered_text(cr, 453.0, 58.0, subtitle, 10.0, false);
    draw_rgb_panel(cr, 34.0, 82.0, 402.0, 316.0, plain_label, plain);
    draw_rgb_panel(cr, 470.0, 82.0, 402.0, 316.0, cipher_label, cipher);
    cairo_show_page(cr);
    check_cairo(cairo_status(cr), "failed rendering RGB PDF");
    cairo_destroy(cr);
    cairo_surface_finish(surface);
    check_cairo(cairo_surface_status(surface), "failed finalizing RGB PDF");
    cairo_surface_destroy(surface);
}

}  // namespace xormap_color
