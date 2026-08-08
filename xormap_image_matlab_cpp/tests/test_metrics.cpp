#include "xormap_image/metrics.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

constexpr double kTightTolerance = 1.0e-12;

}  // namespace

TEST_CASE("entropy matches MATLAB golden values", "[metrics][matlab]")
{
    const Bytes matlab_input = {0, 0, 1, 1, 2, 3};
    REQUIRE(xormap_image::shannon_entropy(matlab_input) ==
            Catch::Approx(1.91829583405449).margin(kTightTolerance));

    REQUIRE(xormap_image::shannon_entropy(Bytes(32, 7)) == 0.0);
    REQUIRE(xormap_image::shannon_entropy({0, 1, 2, 3}, 2) ==
            Catch::Approx(2.0).margin(kTightTolerance));

    Bytes every_byte(256);
    for (std::size_t index = 0; index < every_byte.size(); ++index) {
        every_byte[index] = static_cast<std::uint8_t>(index);
    }
    REQUIRE(xormap_image::shannon_entropy(every_byte) ==
            Catch::Approx(8.0).margin(kTightTolerance));
}

TEST_CASE("entropy rejects invalid histograms", "[metrics][errors]")
{
    REQUIRE_THROWS_AS(xormap_image::shannon_entropy({}), std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::shannon_entropy({0}, 0),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::shannon_entropy({0}, 9),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::shannon_entropy({0, 4}, 2),
                      std::invalid_argument);
}

TEST_CASE("adjacent direction names map to the public enum", "[metrics][adjacent]")
{
    REQUIRE(xormap_image::adjacent_direction_from_string("horizontal") ==
            xormap_image::AdjacentDirection::Horizontal);
    REQUIRE(xormap_image::adjacent_direction_from_string("vertical") ==
            xormap_image::AdjacentDirection::Vertical);
    REQUIRE(xormap_image::adjacent_direction_from_string("diagonal") ==
            xormap_image::AdjacentDirection::Diagonal);
    REQUIRE_THROWS_AS(xormap_image::adjacent_direction_from_string("Horizontal"),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::adjacent_direction_from_string("unknown"),
                      std::invalid_argument);
}

TEST_CASE("adjacent correlations are reproducible and return requested pairs",
          "[metrics][adjacent]")
{
    const Bytes pixels = {
        0, 1, 2, 3,
        4, 5, 6, 7,
        8, 9, 10, 11,
    };

    struct DirectionCase {
        xormap_image::AdjacentDirection direction;
        std::string_view name;
        unsigned int neighbour_delta;
    };
    const DirectionCase cases[] = {
        {xormap_image::AdjacentDirection::Horizontal, "horizontal", 1U},
        {xormap_image::AdjacentDirection::Vertical, "vertical", 4U},
        {xormap_image::AdjacentDirection::Diagonal, "diagonal", 5U},
    };

    for (const auto& test_case : cases) {
        const auto first = xormap_image::adjacent_correlation(
            pixels, 4, 3, test_case.direction, 128, 2026U, true);
        const auto repeated = xormap_image::adjacent_correlation(
            pixels, 4, 3, test_case.direction, 128, 2026U, true);
        const auto by_name = xormap_image::adjacent_correlation(
            pixels, 4, 3, test_case.name, 128, 2026U, false);

        REQUIRE(first.correlation ==
                Catch::Approx(1.0).margin(kTightTolerance));
        REQUIRE(repeated.correlation == first.correlation);
        REQUIRE(by_name.correlation == first.correlation);
        REQUIRE(first.pairs.has_value());
        REQUIRE(repeated.pairs.has_value());
        REQUIRE_FALSE(by_name.pairs.has_value());
        REQUIRE(first.pairs->x == repeated.pairs->x);
        REQUIRE(first.pairs->y == repeated.pairs->y);
        REQUIRE(first.pairs->x.size() == 128);
        REQUIRE(first.pairs->y.size() == 128);

        for (std::size_t index = 0; index < first.pairs->x.size(); ++index) {
            REQUIRE(static_cast<unsigned int>(first.pairs->y[index]) ==
                    static_cast<unsigned int>(first.pairs->x[index]) +
                        test_case.neighbour_delta);
        }
    }

    const auto constant = xormap_image::adjacent_correlation(
        Bytes(9, 42), 3, 3, xormap_image::AdjacentDirection::Horizontal,
        32, 7U);
    REQUIRE(std::isnan(constant.correlation));
}

TEST_CASE("adjacent-pair sampling matches MATLAB rng(2026) golden vectors",
          "[metrics][adjacent][matlab]")
{
    const Bytes pixels = {
        0, 1, 2, 3,
        4, 5, 6, 7,
        8, 9, 10, 11,
    };
    struct Golden {
        xormap_image::AdjacentDirection direction;
        Bytes x;
        Bytes y;
        double correlation;
    };
    const std::vector<Golden> goldens = {
        {xormap_image::AdjacentDirection::Horizontal,
         {0, 6, 8, 1, 5, 9, 2, 10, 4, 8, 0, 5},
         {1, 7, 9, 2, 6, 10, 3, 11, 5, 9, 1, 6},
         1.0},
        {xormap_image::AdjacentDirection::Vertical,
         {1, 3, 4, 1, 2, 5, 2, 7, 4, 4, 0, 2},
         {5, 7, 8, 5, 6, 9, 6, 11, 8, 8, 4, 6},
         0.99999999999999989},
        {xormap_image::AdjacentDirection::Diagonal,
         {0, 2, 4, 1, 1, 5, 2, 6, 4, 4, 0, 1},
         {5, 7, 9, 6, 6, 10, 7, 11, 9, 9, 5, 6},
         1.0},
    };

    for (const Golden& golden : goldens) {
        xormap_image::MatlabTwister generator(2026U);
        const auto result = xormap_image::adjacent_correlation(
            pixels, 4U, 3U, golden.direction, 12U, generator, true);
        REQUIRE(result.pairs.has_value());
        REQUIRE(result.pairs->x == golden.x);
        REQUIRE(result.pairs->y == golden.y);
        REQUIRE(result.correlation ==
                Catch::Approx(golden.correlation).margin(1.0e-15));
    }
}

TEST_CASE("MatlabTwister preserves MATLAB's special rng zero state",
          "[metrics][adjacent][matlab]")
{
    xormap_image::MatlabTwister generator(0U);
    REQUIRE(generator.next_uniform() ==
            Catch::Approx(0.81472368639317894).margin(1.0e-16));
    REQUIRE(generator.next_uniform() ==
            Catch::Approx(0.90579193707561922).margin(1.0e-16));
}

TEST_CASE("adjacent correlation rejects invalid shapes and requests",
          "[metrics][adjacent][errors]")
{
    const Bytes square = {0, 1, 2, 3};
    REQUIRE_THROWS_AS(xormap_image::adjacent_correlation(
                          {}, 0, 0,
                          xormap_image::AdjacentDirection::Horizontal, 1),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::adjacent_correlation(
                          square, 0, 2,
                          xormap_image::AdjacentDirection::Horizontal, 1),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::adjacent_correlation(
                          square, 2, 0,
                          xormap_image::AdjacentDirection::Horizontal, 1),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::adjacent_correlation(
                          square, 3, 2,
                          xormap_image::AdjacentDirection::Horizontal, 1),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::adjacent_correlation(
                          square, std::numeric_limits<std::size_t>::max(), 2,
                          xormap_image::AdjacentDirection::Horizontal, 1),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::adjacent_correlation(
                          square, 2, 2,
                          xormap_image::AdjacentDirection::Horizontal, 0),
                      std::invalid_argument);

    REQUIRE_THROWS_AS(xormap_image::adjacent_correlation(
                          {1, 2}, 1, 2,
                          xormap_image::AdjacentDirection::Horizontal, 1),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::adjacent_correlation(
                          {1, 2}, 2, 1,
                          xormap_image::AdjacentDirection::Vertical, 1),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::adjacent_correlation(
                          {1, 2}, 1, 2,
                          xormap_image::AdjacentDirection::Diagonal, 1),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::adjacent_correlation(
                          square, 2, 2,
                          static_cast<xormap_image::AdjacentDirection>(99), 1),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::adjacent_correlation(
                          square, 2, 2, "sideways", 1),
                      std::invalid_argument);
}

TEST_CASE("NPCR and UACI match MATLAB golden values", "[metrics][matlab]")
{
    const auto measured =
        xormap_image::npcr_uaci({0, 1, 2, 3}, {0, 3, 1, 3});
    REQUIRE(measured.npcr_percent ==
            Catch::Approx(50.0).margin(kTightTolerance));
    REQUIRE(measured.uaci_percent ==
            Catch::Approx(0.294117647058824).margin(kTightTolerance));

    const auto unchanged = xormap_image::npcr_uaci({0, 15}, {0, 15}, 15.0);
    REQUIRE(unchanged.npcr_percent == 0.0);
    REQUIRE(unchanged.uaci_percent == 0.0);

    const auto ideal8 = xormap_image::npcr_uaci_ideal();
    REQUIRE(ideal8.npcr_percent ==
            Catch::Approx(99.609375).margin(kTightTolerance));
    REQUIRE(ideal8.uaci_percent ==
            Catch::Approx(33.4635416666667).margin(kTightTolerance));

    const auto ideal1 = xormap_image::npcr_uaci_ideal(1);
    REQUIRE(ideal1.npcr_percent == 50.0);
    REQUIRE(ideal1.uaci_percent == 50.0);
    const auto ideal63 = xormap_image::npcr_uaci_ideal(63);
    REQUIRE(ideal63.npcr_percent <= 100.0);
    REQUIRE(ideal63.uaci_percent > 33.0);
}

TEST_CASE("NPCR and UACI reject invalid inputs", "[metrics][errors]")
{
    const Bytes empty;
    REQUIRE_THROWS_AS(xormap_image::npcr_uaci(empty, empty),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::npcr_uaci({0}, {}),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::npcr_uaci({0}, {0, 1}),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::npcr_uaci({0}, {0}, 0.0),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::npcr_uaci({0}, {0}, -1.0),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::npcr_uaci(
                          {0}, {0}, std::numeric_limits<double>::infinity()),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::npcr_uaci(
                          {0}, {0}, std::numeric_limits<double>::quiet_NaN()),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::npcr_uaci({16}, {0}, 15.0),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::npcr_uaci_ideal(0), std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::npcr_uaci_ideal(64), std::invalid_argument);
}

TEST_CASE("chi-square uniformity matches the MATLAB implementation",
          "[metrics][matlab]")
{
    Bytes uniform(256);
    for (std::size_t index = 0; index < uniform.size(); ++index) {
        uniform[index] = static_cast<std::uint8_t>(index);
    }

    const auto result = xormap_image::chi_square_uniformity(uniform);
    REQUIRE(result.statistic == 0.0);
    REQUIRE(result.degrees_of_freedom == 255);
    REQUIRE(result.critical_value ==
            Catch::Approx(293.246542359635).margin(1.0e-9));
    REQUIRE(result.passes);

    const auto biased = xormap_image::chi_square_uniformity(Bytes(256, 0));
    REQUIRE(biased.statistic == Catch::Approx(65280.0));
    REQUIRE_FALSE(biased.passes);

    const auto two_bit = xormap_image::chi_square_uniformity(
        {0, 1, 2, 3, 0, 1, 2, 3}, 2, 0.05);
    REQUIRE(two_bit.statistic == 0.0);
    REQUIRE(two_bit.degrees_of_freedom == 3);
    REQUIRE(two_bit.passes);
}

TEST_CASE("chi-square uniformity rejects invalid inputs", "[metrics][errors]")
{
    REQUIRE_THROWS_AS(xormap_image::chi_square_uniformity({}),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::chi_square_uniformity({0}, 0),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::chi_square_uniformity({0}, 9),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::chi_square_uniformity({4}, 2),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::chi_square_uniformity({0}, 8, 0.0),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::chi_square_uniformity({0}, 8, 1.0),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::chi_square_uniformity(
                          {0}, 8, std::numeric_limits<double>::infinity()),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::chi_square_uniformity(
                          {0}, 8,
                          std::numeric_limits<double>::quiet_NaN()),
                      std::invalid_argument);
}

TEST_CASE("PSNR matches MATLAB golden values and its lossless limit",
          "[metrics][matlab]")
{
    const auto result = xormap_image::psnr_db({0, 10}, {0, 20});
    REQUIRE(result.mse == Catch::Approx(50.0).margin(kTightTolerance));
    REQUIRE(result.psnr_db ==
            Catch::Approx(31.1411035653189).margin(kTightTolerance));

    const auto identical = xormap_image::psnr_db({0, 255}, {0, 255});
    REQUIRE(identical.mse == 0.0);
    REQUIRE(std::isinf(identical.psnr_db));
    REQUIRE(identical.psnr_db > 0.0);

    const auto custom_peak = xormap_image::psnr_db({0, 10}, {0, 20}, 20.0);
    REQUIRE(custom_peak.mse == Catch::Approx(50.0));
    REQUIRE(custom_peak.psnr_db ==
            Catch::Approx(10.0 * std::log10(8.0)).margin(kTightTolerance));
}

TEST_CASE("PSNR rejects invalid inputs", "[metrics][errors]")
{
    const Bytes empty;
    REQUIRE_THROWS_AS(xormap_image::psnr_db(empty, empty),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::psnr_db({0}, {}), std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::psnr_db({0}, {0, 1}),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::psnr_db({0}, {0}, 0.0),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::psnr_db({0}, {0}, -1.0),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::psnr_db(
                          {0}, {0}, std::numeric_limits<double>::infinity()),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::psnr_db(
                          {0}, {0}, std::numeric_limits<double>::quiet_NaN()),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::psnr_db({16}, {0}, 15.0),
                      std::invalid_argument);
}

TEST_CASE("difference diagnostics follow MATLAB indexing and checksum rules",
          "[metrics][matlab]")
{
    REQUIRE(xormap_image::diff_checksum({0, 1, 2, 255}) == 1028U);
    REQUIRE(xormap_image::diff_checksum({10, 20, 30}, {10, 21, 28}) == 8U);

    Bytes beyond_checksum_window(4097, 0);
    beyond_checksum_window.back() = 255;
    REQUIRE(xormap_image::diff_checksum(beyond_checksum_window) == 0U);

    REQUIRE(xormap_image::first_differing_byte({0, 0, 7, 0}) == 3U);
    REQUIRE(xormap_image::first_differing_byte({0, 0, 0}) == 0U);
    REQUIRE(xormap_image::first_differing_byte({4, 5, 6}, {4, 8, 6}) == 2U);
    REQUIRE(xormap_image::first_differing_byte({4, 5, 6}, {4, 5, 6}) == 0U);
}

TEST_CASE("difference diagnostics reject empty and mismatched inputs",
          "[metrics][errors]")
{
    const Bytes empty;
    REQUIRE_THROWS_AS(xormap_image::diff_checksum(empty),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::diff_checksum(empty, empty),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::diff_checksum({0}, {0, 1}),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::first_differing_byte(empty),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::first_differing_byte(empty, empty),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::first_differing_byte({0}, {0, 1}),
                      std::invalid_argument);
}
