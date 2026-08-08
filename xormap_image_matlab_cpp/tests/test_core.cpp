#include "fixtures/matlab_vectors.hpp"
#include "xormap_image/core.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

xormap_image::Bits bits_from_text(std::string_view text)
{
    xormap_image::Bits bits;
    bits.reserve(text.size());
    for (char value : text) {
        REQUIRE((value == '0' || value == '1'));
        bits.push_back(static_cast<xormap_image::Byte>(value - '0'));
    }
    return bits;
}

unsigned int hex_digit(char value)
{
    if (value >= '0' && value <= '9') {
        return static_cast<unsigned int>(value - '0');
    }
    const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(value)));
    REQUIRE((upper >= 'A' && upper <= 'F'));
    return 10U + static_cast<unsigned int>(upper - 'A');
}

xormap_image::Bytes bytes_from_hex(std::string_view text)
{
    REQUIRE(text.size() % 2 == 0);
    xormap_image::Bytes bytes;
    bytes.reserve(text.size() / 2);
    for (std::size_t index = 0; index < text.size(); index += 2) {
        bytes.push_back(static_cast<xormap_image::Byte>(
            (hex_digit(text[index]) << 4U) | hex_digit(text[index + 1])));
    }
    return bytes;
}

xormap_image::Bits patterned_seed(std::size_t k)
{
    xormap_image::Bits result(k, 0);
    for (std::size_t index = 0; index < k; ++index) {
        result[index] = static_cast<xormap_image::Byte>(index % 3 == 0 ? 1 : 0);
    }
    return result;
}

xormap_image::Bits matlab_deterministic_bits(std::size_t k)
{
    xormap_image::Bits result(k, 0);
    constexpr std::uint64_t salt = 11;
    for (std::size_t index = 0; index < k; ++index) {
        const auto i = static_cast<std::uint64_t>(index);
        const std::uint64_t value = i * (73U + salt) + (i / 3U) * 19U +
                                    11U * salt + 5U;
        result[index] = static_cast<xormap_image::Byte>(((value & 1U) != 0U) ^
                                                        ((value & 8U) != 0U));
    }
    return result;
}

const xormap_image::Bytes fixture_plain = {
    0, 1, 255, 127, 64, 200, 17, 42, 99, 128,
    13, 240, 7, 181, 55, 170, 85, 100, 3, 222};

}  // namespace

TEST_CASE("byte packing is MATLAB-compatible and round trips", "[core][bits]")
{
    const xormap_image::Bytes bytes = {0, 1, 2, 127, 128, 255};
    const auto bits = xormap_image::bytes_to_bits(bytes);
    REQUIRE(bits.size() == bytes.size() * 8);
    REQUIRE(std::vector<xormap_image::Byte>(bits.begin(), bits.begin() + 8) ==
            xormap_image::Bits{0, 0, 0, 0, 0, 0, 0, 0});
    REQUIRE(std::vector<xormap_image::Byte>(bits.end() - 8, bits.end()) ==
            xormap_image::Bits{1, 1, 1, 1, 1, 1, 1, 1});
    REQUIRE(xormap_image::bits_to_bytes(bits) == bytes);
    REQUIRE_THROWS_AS(xormap_image::bits_to_bytes({1, 0, 1}), std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::bits_to_bytes({0, 2, 0, 0, 0, 0, 0, 0}),
                      std::invalid_argument);
}

TEST_CASE("SHA-256 KDF and secret key match MATLAB goldens", "[core][matlab]")
{
    REQUIRE(xormap_image::sha256_bytes({}) == bytes_from_hex(
        "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855"));

    const xormap_image::Bytes data = {0, 1, 2, 127, 128, 255};
    const auto expanded = xormap_image::hash_expand_bits(data, 512);
    REQUIRE(xormap_image::bits_to_bytes(expanded) ==
            bytes_from_hex(matlab_vectors::kdf_512_hex));
    REQUIRE(xormap_image::hash_expand_bits(data, 5) ==
            xormap_image::Bits{1, 0, 1, 0, 1});
    const auto expected_key = bits_from_text(matlab_vectors::secret_key_512);
    REQUIRE(xormap_image::secret_key(512) == expected_key);
    REQUIRE(xormap_image::secret_key(31) ==
            xormap_image::Bits(expected_key.begin(), expected_key.begin() + 31));
    REQUIRE(xormap_image::hash_expand_bits(data, 0).empty());
    REQUIRE_THROWS_AS(xormap_image::hash_expand_bits({}, 65537), std::length_error);
}

TEST_CASE("canonical and prefix-XOR transforms agree on a complete linear basis",
          "[core][transform]")
{
    for (std::size_t k = 5; k <= 129; ++k) {
        const auto plan = xormap_image::make_transform_plan(k);
        for (std::size_t bit = 0; bit < k; ++bit) {
            xormap_image::Bits basis(k, 0);
            basis[bit] = 1;
            REQUIRE(xormap_image::transform_fast(basis, plan) ==
                    xormap_image::transform_canonical(basis));
        }
    }
    REQUIRE_THROWS_AS(xormap_image::make_transform_plan(4), std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::transform_canonical({0, 0, 0, 0}),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::transform_fast({0, 0, 0, 0, 2}),
                      std::invalid_argument);
}

TEST_CASE("keystream bytes match MATLAB for aligned and unaligned K", "[core][matlab]")
{
    struct Vector {
        std::size_t k;
        std::string_view hex;
    };
    const Vector vectors[] = {
        {5, matlab_vectors::keystream_k5_hex},
        {8, matlab_vectors::keystream_k8_hex},
        {24, matlab_vectors::keystream_k24_hex},
        {31, matlab_vectors::keystream_k31_hex},
    };
    for (const auto& vector : vectors) {
        const auto seed = patterned_seed(vector.k);
        const auto expected = bytes_from_hex(vector.hex);
        REQUIRE(xormap_image::keystream_canonical(seed, expected.size()) == expected);
        REQUIRE(xormap_image::keystream_fast(seed, expected.size()) == expected);
    }
    REQUIRE(xormap_image::keystream_fast(patterned_seed(5), 0).empty());
}

TEST_CASE("encryption vectors preserve MATLAB row-major layout", "[core][matlab]")
{
    const auto k24 = xormap_image::encrypt_fast(fixture_plain, xormap_image::secret_key(24));
    REQUIRE(k24.seed == bits_from_text(matlab_vectors::encrypt_k24_seed));
    REQUIRE(k24.cipher == bytes_from_hex(matlab_vectors::encrypt_k24_hex));
    REQUIRE(k24.iterations == 7);
    REQUIRE(xormap_image::decrypt_canonical(k24.cipher, k24.seed) == fixture_plain);

    const auto k31 = xormap_image::encrypt_fast(fixture_plain, xormap_image::secret_key(31));
    REQUIRE(k31.seed == bits_from_text(matlab_vectors::encrypt_k31_seed));
    REQUIRE(k31.cipher == bytes_from_hex(matlab_vectors::encrypt_k31_hex));
    REQUIRE(k31.iterations == 6);
    REQUIRE(xormap_image::decrypt_fast(k31.cipher, k31.seed) == fixture_plain);
}

TEST_CASE("translated test_xormap_gray_fast assertions all hold", "[core][translated]")
{
    constexpr std::size_t num_pixels = 20;
    for (std::size_t k = 24; k <= 384; k += 24) {
        const auto plan = xormap_image::make_transform_plan(k);
        for (std::size_t input_bit = 0; input_bit < k; ++input_bit) {
            xormap_image::Bits input(k, 0);
            input[input_bit] = 1;
            REQUIRE(xormap_image::transform_fast(input, plan) ==
                    xormap_image::transform_canonical(input));
        }

        const auto seed = matlab_deterministic_bits(k);
        for (std::size_t num_bytes : {std::size_t{1}, std::size_t{7}, num_pixels,
                                      3 * k + 5}) {
            REQUIRE(xormap_image::keystream_fast(seed, num_bytes) ==
                    xormap_image::keystream_canonical(seed, num_bytes));
        }

        const auto key = xormap_image::secret_key(k);
        const auto fast = xormap_image::encrypt_fast(fixture_plain, key);
        const auto canonical = xormap_image::encrypt_canonical(fixture_plain, key);
        REQUIRE(fast.seed == canonical.seed);
        REQUIRE(fast.cipher.size() == fixture_plain.size());
        REQUIRE(fast.cipher == canonical.cipher);
        REQUIRE(xormap_image::decrypt_canonical(fast.cipher, fast.seed) == fixture_plain);
        REQUIRE(fast.iterations == (num_pixels * 8 + k - 1) / k);
    }
}

TEST_CASE("cipher rejects invalid keys and seeds", "[core][errors]")
{
    REQUIRE_THROWS_AS(xormap_image::encrypt_fast({1, 2, 3}, {0, 1, 0, 1}),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::encrypt_fast({1, 2, 3}, {0, 1, 0, 1, 2}),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::decrypt_fast({1, 2, 3}, {1, 0, 1, 0}),
                      std::invalid_argument);
}
