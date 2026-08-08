#include "xormap_color/common.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <filesystem>
#include <fstream>
#include <random>

namespace {

std::filesystem::path temporary_path(const std::string& suffix)
{
    static std::mt19937_64 generator(0xC0110AULL);
    return std::filesystem::temp_directory_path() /
           ("xormap-color-" + std::to_string(generator()) + suffix);
}

}  // namespace

TEST_CASE("RGB888 packing is lossless and uses RRGGBB words")
{
    const xormap_color::RgbImage image(
        2, 2, {0, 1, 2, 255, 128, 64, 17, 34, 51, 5, 6, 7});
    const xormap_color::Words packed = xormap_color::pack_rgb888(image);
    REQUIRE(packed == xormap_color::Words{0x000102U, 0xFF8040U,
                                          0x112233U, 0x050607U});
    REQUIRE(xormap_color::unpack_rgb888(packed, 2, 2) == image);
}

TEST_CASE("RGB565 conversion truncates channels and previews by replication")
{
    const xormap_color::RgbImage image(2, 1, {255, 255, 255, 7, 3, 7});
    const xormap_color::Words packed = xormap_color::pack_rgb565(image);
    REQUIRE(packed == xormap_color::Words{0xFFFFU, 0x0000U});
    REQUIRE(xormap_color::preview_rgb565(packed, 2, 1).pixels ==
            std::vector<std::uint8_t>{255, 255, 255, 0, 0, 0});
}

TEST_CASE("word cipher fast and canonical streams match at 16 and 24 bits")
{
    for (const unsigned int width : {16U, 24U}) {
        for (const std::size_t k : {5U, 24U, 31U, 48U}) {
            xormap_image::Bits seed(k);
            for (std::size_t i = 0; i < k; ++i) {
                seed[i] = static_cast<std::uint8_t>(((i * 37U + k) >> 2U) & 1U);
            }
            REQUIRE(xormap_color::keystream_words_fast(seed, width, 37) ==
                    xormap_color::keystream_words_canonical(seed, width, 37));
        }
    }
}

TEST_CASE("word encryption round trips for aligned and unaligned K")
{
    const xormap_color::Words plain{0x000000U, 0x123456U, 0xFFFFFFU,
                                    0xABCDEFU, 0x010203U};
    for (const std::size_t k : {5U, 24U, 31U, 384U}) {
        xormap_image::Bits key(k);
        for (std::size_t i = 0; i < k; ++i) {
            key[i] = static_cast<std::uint8_t>((i * 11U + 3U) & 1U);
        }
        const auto encrypted = xormap_color::encrypt_words_fast(plain, 24U, key);
        REQUIRE(xormap_color::decrypt_words_fast(
                    encrypted.cipher, 24U, encrypted.seed) == plain);
        REQUIRE(encrypted.iterations == (plain.size() * 24U + k - 1U) / k);
    }
}

TEST_CASE("RGB TIFF and self-describing RGB565 files round trip")
{
    const xormap_color::RgbImage image(
        3, 2, {0, 1, 2, 3, 4, 5, 6, 7, 8,
               255, 254, 253, 128, 64, 32, 9, 10, 11});
    const auto tiff = temporary_path(".tiff");
    const auto raw = temporary_path(".rgb565");
    xormap_color::write_tiff_rgb8(tiff, image);
    REQUIRE(xormap_color::load_tiff_rgb8(tiff) == image);

    const auto packed = xormap_color::pack_rgb565(image);
    xormap_color::write_rgb565_raw(raw, packed, image.width, image.height);
    std::size_t width = 0;
    std::size_t height = 0;
    REQUIRE(xormap_color::read_rgb565_raw(raw, width, height) == packed);
    REQUIRE(width == image.width);
    REQUIRE(height == image.height);
    std::filesystem::remove(tiff);
    std::filesystem::remove(raw);
}

TEST_CASE("RGB comparison report is a nonempty PDF")
{
    const xormap_color::RgbImage plain(
        2, 2, {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255});
    const xormap_color::RgbImage cipher(
        2, 2, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12});
    const auto path = temporary_path(".pdf");
    xormap_color::write_rgb_comparison_pdf(
        path, "RGB comparison", "synthetic fixture", plain, cipher,
        "Plain", "Cipher");
    REQUIRE(std::filesystem::file_size(path) > 1000U);
    std::ifstream input(path, std::ios::binary);
    std::string magic(4, '\0');
    input.read(magic.data(), 4);
    REQUIRE(magic == "%PDF");
    std::filesystem::remove(path);
}

TEST_CASE("packed-word NPCR UACI and diagnostics use word values")
{
    const xormap_color::Words first{0U, 10U, 20U, 30U};
    const xormap_color::Words second{0U, 11U, 18U, 30U};
    const auto metrics = xormap_color::npcr_uaci_words(first, second, 255U);
    REQUIRE(metrics.npcr_percent == 50.0);
    REQUIRE(metrics.uaci_percent == Catch::Approx(100.0 * 3.0 / (4.0 * 255.0)));
    REQUIRE(xormap_color::first_differing_word({0U, 0U, 4U}) == 3U);
    REQUIRE(xormap_color::diff_checksum_words({1U, 2U, 3U}) == 14U);
}
