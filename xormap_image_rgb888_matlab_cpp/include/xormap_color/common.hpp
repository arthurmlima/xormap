#pragma once

#include "xormap_image/core.hpp"
#include "xormap_image/metrics.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace xormap_color {

using Word = std::uint32_t;
using Words = std::vector<Word>;

struct RgbImage {
    std::size_t width = 0;
    std::size_t height = 0;
    std::vector<std::uint8_t> pixels;

    RgbImage() = default;
    RgbImage(std::size_t image_width, std::size_t image_height,
             std::vector<std::uint8_t> interleaved_rgb);

    [[nodiscard]] std::size_t size() const noexcept { return width * height; }
    void validate() const;
    friend bool operator==(const RgbImage& lhs, const RgbImage& rhs) noexcept;
};

struct ColorManifestEntry {
    std::string filename;
    std::string volume;
    std::string name;
    std::filesystem::path path;
};

[[nodiscard]] RgbImage load_tiff_rgb8(const std::filesystem::path& path);
void write_tiff_rgb8(const std::filesystem::path& path, const RgbImage& image);

[[nodiscard]] std::vector<ColorManifestEntry> read_color_manifest(
    const std::filesystem::path& manifest_path,
    const std::filesystem::path& image_directory = {});

[[nodiscard]] Words pack_rgb888(const RgbImage& image);
[[nodiscard]] RgbImage unpack_rgb888(const Words& words,
                                     std::size_t width,
                                     std::size_t height);
[[nodiscard]] Words pack_rgb565(const RgbImage& image);
[[nodiscard]] RgbImage preview_rgb565(const Words& words,
                                      std::size_t width,
                                      std::size_t height);

[[nodiscard]] std::array<std::vector<std::uint8_t>, 3>
rgb888_channels(const Words& words);
[[nodiscard]] std::array<std::vector<std::uint8_t>, 3>
rgb565_channels(const Words& words);

[[nodiscard]] xormap_image::Bytes words_to_bytes_be(
    const Words& words, unsigned int word_bits);
[[nodiscard]] Words keystream_words_fast(
    const xormap_image::Bits& seed, unsigned int word_bits,
    std::size_t num_words);
[[nodiscard]] Words keystream_words_canonical(
    const xormap_image::Bits& seed, unsigned int word_bits,
    std::size_t num_words);

struct WordEncryptionResult {
    Words cipher;
    xormap_image::Bits seed;
    std::size_t iterations = 0;
};

[[nodiscard]] WordEncryptionResult encrypt_words_fast(
    const Words& plain, unsigned int word_bits,
    const xormap_image::Bits& key_bits);
[[nodiscard]] WordEncryptionResult encrypt_words_canonical(
    const Words& plain, unsigned int word_bits,
    const xormap_image::Bits& key_bits);
[[nodiscard]] Words decrypt_words_fast(
    const Words& cipher, unsigned int word_bits,
    const xormap_image::Bits& seed);

[[nodiscard]] xormap_image::NpcrUaciResult npcr_uaci_words(
    const Words& first, const Words& second, Word max_value);
[[nodiscard]] xormap_image::PsnrResult psnr_words(
    const Words& first, const Words& second, double max_value);
[[nodiscard]] std::uint32_t diff_checksum_words(const Words& difference);
[[nodiscard]] std::size_t first_differing_word(const Words& difference);

void write_rgb565_raw(const std::filesystem::path& path,
                      const Words& words,
                      std::size_t width,
                      std::size_t height);
[[nodiscard]] Words read_rgb565_raw(const std::filesystem::path& path,
                                    std::size_t& width,
                                    std::size_t& height);

void write_rgb_comparison_pdf(const std::filesystem::path& path,
                              const std::string& title,
                              const std::string& subtitle,
                              const RgbImage& plain,
                              const RgbImage& cipher,
                              const std::string& plain_label,
                              const std::string& cipher_label);

}  // namespace xormap_color
