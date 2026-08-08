#include "xormap_color/common.hpp"
#include "xormap_rgb565/evaluation.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>

namespace {

std::filesystem::path project_root()
{
    return std::filesystem::path(__FILE__).parent_path().parent_path();
}

}  // namespace

TEST_CASE("RGB565 K grids are validated")
{
    REQUIRE(xormap_rgb565::k_values(24, 24, 96) ==
            std::vector<std::size_t>{24, 48, 72, 96});
    REQUIRE_THROWS_AS(xormap_rgb565::k_values(4, 1, 8), std::invalid_argument);
}

TEST_CASE("RGB565 word cipher round trips")
{
    const xormap_color::Words plain{0U, 1U, 0x1234U, 0xFFFFU, 0xABCDU};
    for (const std::size_t k : {8U, 12U, 24U, 31U, 512U}) {
        xormap_image::Bits key(k, 0U);
        for (std::size_t i = 0; i < k; ++i) {
            key[i] = static_cast<std::uint8_t>((i * 7U + 1U) & 1U);
        }
        const auto encrypted = xormap_color::encrypt_words_fast(plain, 16U, key);
        REQUIRE(xormap_color::decrypt_words_fast(
                    encrypted.cipher, 16U, encrypted.seed) == plain);
    }
}

TEST_CASE("all 51 converted RGB565 files match their RGB888 sources")
{
    const auto root = project_root();
    const auto manifest = root / "images_rgb565" / "manifest.csv";
    const auto source_root = root.parent_path() / "xormap_image_rgb888_matlab";
    if (!std::filesystem::exists(manifest)) {
        SKIP("generated full RGB565 corpus is not installed");
    }
    const auto sources = xormap_color::read_color_manifest(
        source_root / "images" / "manifest.csv", source_root / "images");
    REQUIRE(sources.size() == 51U);
    for (const auto& source : sources) {
        const auto rgb = xormap_color::load_tiff_rgb8(source.path);
        std::size_t width = 0;
        std::size_t height = 0;
        const auto packed = xormap_color::read_rgb565_raw(
            root / "images_rgb565" / (source.name + ".rgb565"), width, height);
        REQUIRE(width == rgb.width);
        REQUIRE(height == rgb.height);
        REQUIRE(packed == xormap_color::pack_rgb565(rgb));
    }
}

TEST_CASE("Mandrill K512 RGB565 metrics match the published MATLAB results")
{
    const auto path = project_root() / "images_rgb565" / "4.2.03.rgb565";
    if (!std::filesystem::exists(path)) {
        SKIP("generated RGB565 corpus is not installed");
    }
    std::size_t width = 0;
    std::size_t height = 0;
    const auto plain = xormap_color::read_rgb565_raw(path, width, height);
    auto changed = plain;
    changed[(height / 2U) * width + width / 2U] ^= 1U;
    const auto key = xormap_image::secret_key(512U);
    const auto encrypted = xormap_color::encrypt_words_fast(plain, 16U, key);
    const auto cipher2 = xormap_color::encrypt_words_fast(changed, 16U, key).cipher;
    const auto plain_channels = xormap_color::rgb565_channels(plain);
    const auto cipher_channels = xormap_color::rgb565_channels(encrypted.cipher);
    REQUIRE(xormap_image::shannon_entropy(plain_channels[0], 5) ==
            Catch::Approx(4.7163).margin(0.00005));
    REQUIRE(xormap_image::shannon_entropy(cipher_channels[0], 5) ==
            Catch::Approx(4.9999).margin(0.00005));
    REQUIRE(xormap_image::shannon_entropy(plain_channels[1], 6) ==
            Catch::Approx(5.4778).margin(0.00005));
    REQUIRE(xormap_image::shannon_entropy(cipher_channels[1], 6) ==
            Catch::Approx(5.9998).margin(0.00005));
    const auto differential = xormap_color::npcr_uaci_words(
        encrypted.cipher, cipher2, 0xFFFFU);
    REQUIRE(differential.npcr_percent ==
            Catch::Approx(99.9966).margin(0.00005));
    REQUIRE(differential.uaci_percent ==
            Catch::Approx(33.3651).margin(0.00005));
    REQUIRE(xormap_color::decrypt_words_fast(
                encrypted.cipher, 16U, encrypted.seed) == plain);
}
