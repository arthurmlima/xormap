#include "xormap_image/threefry.hpp"
#include "xormap_image/metrics.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace {

std::uint64_t double_bits(double value)
{
    std::uint64_t bits = 0U;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

}  // namespace

TEST_CASE("Threefry4x64-20 matches the published Random123 known answer",
          "[threefry][kat]")
{
    const xormap_image::MatlabThreefry::Block zero{{0U, 0U, 0U, 0U}};
    const xormap_image::MatlabThreefry::Block expected{{
        UINT64_C(0x09218EBDE6C85537),
        UINT64_C(0x55941F5266D86105),
        UINT64_C(0x4BD25E16282434DC),
        UINT64_C(0xEE29EC846BD2E40B),
    }};
    REQUIRE(xormap_image::MatlabThreefry::generate_block(zero, zero) ==
            expected);
}

TEST_CASE("MATLAB Threefry seed one matches exact full-precision rand bits",
          "[threefry][matlab]")
{
    // R2026a: rng(1,'threefry'); num2hex(rand(1,8))
    constexpr std::array<std::uint64_t, 8> expected{{
        UINT64_C(0x3FC1F9ED9161FC67),
        UINT64_C(0x3FEA3B58E04B7C5E),
        UINT64_C(0x3FBB7653244DD05F),
        UINT64_C(0x3FDA700EAE58E551),
        UINT64_C(0x3FE40610CEEE6248),
        UINT64_C(0x3FDE39D5AB0221B5),
        UINT64_C(0x3FED9921455C5765),
        UINT64_C(0x3F918B533ADE7D7F),
    }};

    xormap_image::MatlabThreefry generator(1U);
    for (const std::uint64_t bits : expected) {
        REQUIRE(double_bits(generator.next_uniform()) == bits);
    }
}

TEST_CASE("MATLAB Threefry bounded integers match randi across blocks",
          "[threefry][matlab]")
{
    // R2026a: rng(1,'threefry'); randi(3,1,12); randi(4,1,12)
    constexpr std::array<std::size_t, 12> expected_three{{
        1U, 3U, 1U, 2U, 2U, 2U, 3U, 1U, 1U, 1U, 3U, 3U,
    }};
    constexpr std::array<std::size_t, 12> expected_four{{
        4U, 1U, 4U, 1U, 2U, 4U, 1U, 2U, 4U, 4U, 2U, 4U,
    }};

    xormap_image::MatlabThreefry generator(1U);
    for (const std::size_t expected : expected_three) {
        REQUIRE(generator.randi(3U) == expected);
    }
    for (const std::size_t expected : expected_four) {
        REQUIRE(generator.randi(4U) == expected);
    }
}

TEST_CASE("MATLAB worker secret keys retain the worker Threefry generator",
          "[threefry][core][matlab][parallel]")
{
    // Produced by secret_key(24) inside a MATLAB worker (equivalently
    // rng(1729,'threefry'); rand(1,24)>0.5).
    const xormap_image::Bits expected{
        0, 0, 1, 0, 1, 1, 0, 0,
        0, 0, 1, 1, 0, 1, 0, 0,
        0, 0, 1, 0, 1, 1, 1, 1,
    };
    REQUIRE(xormap_image::worker_secret_key(24U) == expected);
    REQUIRE(xormap_image::worker_secret_key(0U).empty());

    // SHA-256 of the 384 key bits represented as raw uint8 values. This
    // exercises 96 complete Threefry blocks, not only the first few lanes.
    const xormap_image::Bytes expected_k384_sha{
        0x67, 0xB8, 0x83, 0x19, 0xDF, 0x63, 0x04, 0xA9,
        0xB9, 0x3A, 0x43, 0xCB, 0xA2, 0x0C, 0xD3, 0x4C,
        0x93, 0x51, 0x20, 0x29, 0x58, 0xBF, 0x37, 0x9F,
        0x04, 0x53, 0x62, 0x19, 0x7B, 0xD5, 0xB1, 0x49,
    };
    REQUIRE(xormap_image::sha256_bytes(
                xormap_image::worker_secret_key(384U)) == expected_k384_sha);
}

TEST_CASE("adjacent-pair sampling accepts the MATLAB worker stream",
          "[threefry][metrics][matlab][parallel]")
{
    const xormap_image::Bytes pixels{
        0, 1, 2, 3,
        4, 5, 6, 7,
        8, 9, 10, 11,
    };
    const xormap_image::Bytes expected_x{
        2, 8, 2, 4, 4, 6, 8, 1, 2, 2, 9, 10,
    };
    const xormap_image::Bytes expected_y{
        3, 9, 3, 5, 5, 7, 9, 2, 3, 3, 10, 11,
    };

    xormap_image::MatlabThreefry generator(1U);
    const auto result = xormap_image::adjacent_correlation(
        pixels, 4U, 3U, xormap_image::AdjacentDirection::Horizontal,
        12U, generator, true);
    REQUIRE(result.pairs.has_value());
    REQUIRE(result.pairs->x == expected_x);
    REQUIRE(result.pairs->y == expected_y);
}

TEST_CASE("MATLAB Threefry is copyable, discardable, and validates randi",
          "[threefry][errors]")
{
    xormap_image::MatlabThreefry first(7U);
    xormap_image::MatlabThreefry second = first;
    first.discard_uniform(9U);
    for (std::size_t index = 0U; index < 9U; ++index) {
        static_cast<void>(second.next_uniform());
    }
    REQUIRE(first.next_uniform() == second.next_uniform());

    REQUIRE_THROWS_AS(first.randi(0U), std::invalid_argument);
    if (std::numeric_limits<std::size_t>::max() >
        UINT64_C(9007199254740992)) {
        REQUIRE_THROWS_AS(
            first.randi(static_cast<std::size_t>(UINT64_C(9007199254740993))),
            std::invalid_argument);
    }
}
