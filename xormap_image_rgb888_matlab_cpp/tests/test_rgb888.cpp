#include "xormap_color/common.hpp"
#include "xormap_color/rgb888.hpp"
#include "xormap_image/threefry.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <sstream>

namespace {

std::filesystem::path matlab_images()
{
    return std::filesystem::path(__FILE__).parent_path().parent_path()
        .parent_path() / "xormap_image_rgb888_matlab" / "images";
}

xormap_color::Words xor_words(const xormap_color::Words& first,
                              const xormap_color::Words& second)
{
    xormap_color::Words result(first.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        result[i] = first[i] ^ second[i];
    }
    return result;
}

}  // namespace

TEST_CASE("translated RGB888 verification suite passes")
{
    std::ostringstream progress;
    REQUIRE_NOTHROW(xormap_color::rgb888::verify(progress));
    REQUIRE(progress.str().find("K=384") != std::string::npos);
}

TEST_CASE("real MATLAB RGB888 K24 sweep vector matches exactly")
{
    const auto path = matlab_images() / "4.1.01.tiff";
    if (!std::filesystem::exists(path)) {
        SKIP("local USC-SIPI RGB888 corpus is not installed");
    }
    const auto image = xormap_color::load_tiff_rgb8(path);
    const auto plain = xormap_color::pack_rgb888(image);
    auto changed = plain;
    changed[(image.height / 2U) * image.width + image.width / 2U] ^= 1U;
    const auto key = xormap_image::worker_secret_key(24U);
    const auto cipher = xormap_color::encrypt_words_fast(plain, 24U, key).cipher;
    const auto cipher2 = xormap_color::encrypt_words_fast(changed, 24U, key).cipher;
    const auto channels = xormap_color::rgb888_channels(cipher);
    const double entropy = (xormap_image::shannon_entropy(channels[0]) +
                            xormap_image::shannon_entropy(channels[1]) +
                            xormap_image::shannon_entropy(channels[2])) / 3.0;
    xormap_image::MatlabThreefry rng(1U);
    double correlation = 0.0;
    for (const auto& channel : channels) {
        correlation += std::fabs(xormap_image::adjacent_correlation(
            channel, image.width, image.height,
            xormap_image::AdjacentDirection::Horizontal, 3000U, rng).correlation) / 3.0;
    }
    const auto differential = xormap_color::npcr_uaci_words(
        cipher, cipher2, 0xFFFFFFU);
    REQUIRE(entropy == Catch::Approx(7.995557).margin(0.0000005));
    REQUIRE(correlation == Catch::Approx(0.008826).margin(0.0000005));
    REQUIRE(differential.npcr_percent == 100.0);
    REQUIRE(differential.uaci_percent ==
            Catch::Approx(33.065294).margin(0.0000005));
}

TEST_CASE("real MATLAB RGB888 K24 analysis vector matches exactly")
{
    const auto path = matlab_images() / "4.1.01.tiff";
    if (!std::filesystem::exists(path)) {
        SKIP("local USC-SIPI RGB888 corpus is not installed");
    }
    const auto image = xormap_color::load_tiff_rgb8(path);
    const auto plain = xormap_color::pack_rgb888(image);
    auto key1 = xormap_image::worker_secret_key(24U);
    auto key2 = key1;
    key2[0] ^= 1U;
    const auto encrypted = xormap_color::encrypt_words_fast(plain, 24U, key1);
    const auto cipher2 = xormap_color::encrypt_words_fast(plain, 24U, key2).cipher;
    const auto difference = xor_words(encrypted.cipher, cipher2);
    const auto wrong = xor_words(plain, difference);
    const auto cipher_rgb = xormap_color::unpack_rgb888(
        encrypted.cipher, image.width, image.height);
    const auto cipher2_rgb = xormap_color::unpack_rgb888(
        cipher2, image.width, image.height);
    const auto wrong_rgb = xormap_color::unpack_rgb888(
        wrong, image.width, image.height);
    const auto differential = xormap_color::npcr_uaci_words(
        encrypted.cipher, cipher2, 0xFFFFFFU);
    REQUIRE(differential.npcr_percent == 100.0);
    REQUIRE(differential.uaci_percent ==
            Catch::Approx(33.203141).margin(0.0000005));
    REQUIRE(xormap_image::psnr_db(cipher_rgb.pixels, cipher2_rgb.pixels).psnr_db ==
            Catch::Approx(7.8093).margin(0.00005));
    REQUIRE(xormap_image::psnr_db(image.pixels, wrong_rgb.pixels).psnr_db ==
            Catch::Approx(7.3719).margin(0.00005));
    REQUIRE(xormap_color::first_differing_word(difference) == 1U);
    REQUIRE(xormap_color::diff_checksum_words(difference) == 9858619U);
    REQUIRE(xormap_color::decrypt_words_fast(
                encrypted.cipher, 24U, encrypted.seed) == plain);
}
