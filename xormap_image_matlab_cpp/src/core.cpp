#include "xormap_image/core.hpp"

#include <openssl/evp.h>

#include <algorithm>
#include <limits>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>

namespace xormap_image {
namespace {

constexpr std::size_t kBitsPerByte = 8;
constexpr std::size_t kSha256Bits = 256;
constexpr std::size_t kSha256Bytes = kSha256Bits / kBitsPerByte;
constexpr std::size_t kCounterValues = 256;

void require_valid_k(std::size_t k)
{
    if (k <= 4) {
        throw std::invalid_argument("K must be greater than 4");
    }
    if (k > std::numeric_limits<std::size_t>::max() / 2) {
        throw std::length_error("K is too large");
    }
}

std::size_t checked_bit_count(std::size_t num_bytes)
{
    if (num_bytes > std::numeric_limits<std::size_t>::max() /
                        kBitsPerByte) {
        throw std::length_error("byte count is too large to express in bits");
    }
    return num_bytes * kBitsPerByte;
}

std::size_t iteration_count(std::size_t num_bytes, std::size_t k)
{
    const std::size_t bits = checked_bit_count(num_bytes);
    return bits / k + (bits % k != 0 ? 1U : 0U);
}

void validate_plan(const TransformPlan& plan)
{
    require_valid_k(plan.k);
    if (plan.left.size() != plan.k || plan.right.size() != plan.k) {
        throw std::invalid_argument(
            "transform plan must contain K left and right window indices");
    }
    if (plan.odd_positions.size() != plan.centers.size()) {
        throw std::invalid_argument(
            "transform plan odd-position and center arrays must match");
    }

    for (std::size_t output = 0; output < plan.k; ++output) {
        if (plan.left[output] > plan.right[output] ||
            plan.right[output] >= plan.k) {
            throw std::invalid_argument(
                "transform plan contains an invalid window");
        }
    }
    for (std::size_t i = 0; i < plan.odd_positions.size(); ++i) {
        if (plan.odd_positions[i] >= plan.k ||
            plan.centers[i] >= plan.k) {
            throw std::invalid_argument(
                "transform plan contains an out-of-range index");
        }
    }
}

Bits transform_canonical_unchecked(const Bits& input)
{
    const std::size_t k = input.size();
    Bits result(k, Byte{0});

    // This is xormap_transform.m with its one-based source indices converted
    // at the point where the input vector is accessed.
    const std::size_t start_col = (k - 1) - k / 2;
    for (std::size_t output_bit = 0; output_bit < k; ++output_bit) {
        const std::size_t column = start_col + output_bit;

        std::size_t i;
        std::size_t j;
        if (column <= k - 1) {
            i = k - column;
            j = k;
        } else {
            i = 1;
            j = 2 * k - 1 - column;
        }

        Byte value = 0;
        while (i < j) {
            value ^= input[i - 1];
            value ^= input[j - 1];
            ++i;
            --j;
        }
        result[output_bit] = value;
    }
    return result;
}

Bits transform_fast_unchecked(const Bits& input, const TransformPlan& plan)
{
    Bits prefix(plan.k + 1, Byte{0});
    for (std::size_t i = 0; i < plan.k; ++i) {
        prefix[i + 1] = static_cast<Byte>(prefix[i] ^ input[i]);
    }

    Bits result(plan.k, Byte{0});
    for (std::size_t output = 0; output < plan.k; ++output) {
        result[output] = static_cast<Byte>(
            prefix[plan.right[output] + 1] ^ prefix[plan.left[output]]);
    }
    for (std::size_t i = 0; i < plan.odd_positions.size(); ++i) {
        result[plan.odd_positions[i]] ^= input[plan.centers[i]];
    }
    return result;
}

template <typename Advance>
Bytes make_keystream(const Bits& seed, std::size_t num_bytes,
                     Advance&& advance)
{
    const std::size_t num_bits = checked_bit_count(num_bytes);
    Bytes bytes(num_bytes, Byte{0});
    Bits state = seed;
    std::size_t produced = 0;

    while (produced < num_bits) {
        // Genv_xormap advances x_reg on en before its new state contributes
        // the next output block.  The MATLAB stream functions do likewise.
        state = advance(state);
        const std::size_t take =
            std::min(state.size(), num_bits - produced);
        for (std::size_t bit = 0; bit < take; ++bit, ++produced) {
            if (state[bit] != 0) {
                bytes[produced / kBitsPerByte] |= static_cast<Byte>(
                    Byte{1} << (produced % kBitsPerByte));
            }
        }
    }
    return bytes;
}

Bytes xor_bytes(const Bytes& input, const Bytes& mask)
{
    if (input.size() != mask.size()) {
        throw std::invalid_argument("XOR operands must have equal lengths");
    }
    Bytes output(input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        output[i] = static_cast<Byte>(input[i] ^ mask[i]);
    }
    return output;
}

Bits derive_seed(const Bytes& plain, const Bits& key_bits)
{
    validate_bits(key_bits, "key_bits");
    require_valid_k(key_bits.size());

    Bits seed = hash_expand_bits(plain, key_bits.size());
    for (std::size_t i = 0; i < seed.size(); ++i) {
        seed[i] ^= key_bits[i];
    }
    return seed;
}

struct EvpMdContextDeleter {
    void operator()(EVP_MD_CTX* context) const noexcept
    {
        EVP_MD_CTX_free(context);
    }
};

}  // namespace

void validate_bits(const Bits& bits, const char* argument_name)
{
    const char* const name = argument_name == nullptr ? "bits" : argument_name;
    for (std::size_t i = 0; i < bits.size(); ++i) {
        if (bits[i] != 0 && bits[i] != 1) {
            throw std::invalid_argument(std::string(name) +
                                        " must contain only binary values "
                                        "(bad value at index " +
                                        std::to_string(i) + ")");
        }
    }
}

Bits bytes_to_bits(const Bytes& bytes)
{
    const std::size_t bit_count = checked_bit_count(bytes.size());
    Bits bits(bit_count, Byte{0});
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        for (std::size_t bit = 0; bit < kBitsPerByte; ++bit) {
            bits[i * kBitsPerByte + bit] =
                static_cast<Byte>((bytes[i] >> bit) & Byte{1});
        }
    }
    return bits;
}

Bytes bits_to_bytes(const Bits& bits)
{
    validate_bits(bits);
    if (bits.size() % kBitsPerByte != 0) {
        throw std::invalid_argument(
            "bit count must be a multiple of 8 to pack into bytes");
    }

    Bytes bytes(bits.size() / kBitsPerByte, Byte{0});
    for (std::size_t i = 0; i < bits.size(); ++i) {
        if (bits[i] != 0) {
            bytes[i / kBitsPerByte] |=
                static_cast<Byte>(Byte{1} << (i % kBitsPerByte));
        }
    }
    return bytes;
}

TransformPlan make_transform_plan(std::size_t k)
{
    require_valid_k(k);

    TransformPlan plan;
    plan.k = k;
    plan.left.resize(k);
    plan.right.resize(k);
    plan.odd_positions.reserve(k / 2 + 1);
    plan.centers.reserve(k / 2 + 1);

    const std::size_t start_col = (k - 1) - k / 2;
    for (std::size_t output = 0; output < k; ++output) {
        const std::size_t column = start_col + output;
        if (column <= k - 1) {
            // Convert MATLAB's inclusive one-based [k-column, k].
            plan.left[output] = k - column - 1;
            plan.right[output] = k - 1;
        } else {
            // Convert MATLAB's inclusive one-based [1, 2*k-1-column].
            plan.left[output] = 0;
            plan.right[output] = 2 * k - 2 - column;
        }

        const std::size_t window_length =
            plan.right[output] - plan.left[output] + 1;
        if (window_length % 2 != 0) {
            plan.odd_positions.push_back(output);
            plan.centers.push_back(
                plan.left[output] + window_length / 2);
        }
    }
    return plan;
}

Bits transform_canonical(const Bits& input)
{
    validate_bits(input, "input");
    require_valid_k(input.size());
    return transform_canonical_unchecked(input);
}

Bits transform_fast(const Bits& input, const TransformPlan& plan)
{
    validate_bits(input, "input");
    validate_plan(plan);
    if (input.size() != plan.k) {
        throw std::invalid_argument(
            "input length must equal the transform plan's K");
    }
    return transform_fast_unchecked(input, plan);
}

Bits transform_fast(const Bits& input)
{
    validate_bits(input, "input");
    require_valid_k(input.size());
    const TransformPlan plan = make_transform_plan(input.size());
    return transform_fast_unchecked(input, plan);
}

Bytes sha256_bytes(const Bytes& bytes)
{
    std::unique_ptr<EVP_MD_CTX, EvpMdContextDeleter> context(EVP_MD_CTX_new());
    if (!context) {
        throw std::runtime_error("OpenSSL could not allocate a SHA-256 context");
    }
    if (EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1) {
        throw std::runtime_error("OpenSSL could not initialize SHA-256");
    }
    if (!bytes.empty() &&
        EVP_DigestUpdate(context.get(), bytes.data(), bytes.size()) != 1) {
        throw std::runtime_error("OpenSSL could not update SHA-256");
    }

    Bytes digest(kSha256Bytes, Byte{0});
    unsigned int digest_size = 0;
    if (EVP_DigestFinal_ex(context.get(), digest.data(), &digest_size) != 1 ||
        digest_size != kSha256Bytes) {
        throw std::runtime_error("OpenSSL could not finalize SHA-256");
    }
    return digest;
}

Bits hash_expand_bits(const Bytes& bytes, std::size_t k)
{
    if (k == 0) {
        return {};
    }
    const std::size_t blocks =
        k / kSha256Bits + (k % kSha256Bits != 0 ? 1U : 0U);
    if (blocks > kCounterValues) {
        throw std::length_error(
            "hash expansion exceeds the MATLAB one-byte counter limit "
            "(65536 bits)");
    }
    if (bytes.size() == std::numeric_limits<std::size_t>::max()) {
        throw std::length_error("hash input is too large to append a counter");
    }

    Bits result(k, Byte{0});
    std::size_t produced = 0;
    for (std::size_t counter = 0; counter < blocks; ++counter) {
        Bytes block_input;
        block_input.reserve(bytes.size() + 1);
        block_input.insert(block_input.end(), bytes.begin(), bytes.end());
        block_input.push_back(static_cast<Byte>(counter));

        const Bytes digest = sha256_bytes(block_input);
        for (Byte byte : digest) {
            for (std::size_t bit = 0;
                 bit < kBitsPerByte && produced < k; ++bit, ++produced) {
                result[produced] =
                    static_cast<Byte>((byte >> bit) & Byte{1});
            }
        }
    }
    return result;
}

Bits secret_key(std::size_t k)
{
    // std::mt19937 single-word seeding is the reference init_genrand used by
    // MATLAB's nonzero twister seeds.  MATLAB rand uses genrand_res53: the
    // high 27 bits of one word followed by the high 26 bits of the next.
    std::mt19937 twister(1729U);
    Bits bits(k, Byte{0});
    for (std::size_t i = 0; i < k; ++i) {
        double value;
        do {
            const std::uint32_t high27 = twister() >> 5U;
            const std::uint32_t low26 = twister() >> 6U;
            value = (static_cast<double>(high27) * 67108864.0 +
                     static_cast<double>(low26)) /
                    9007199254740992.0;
            // MATLAB excludes zero from rand's output and resamples it.
        } while (value == 0.0);
        bits[i] = static_cast<Byte>(value > 0.5 ? 1 : 0);
    }
    return bits;
}

Bytes keystream_canonical(const Bits& seed, std::size_t num_bytes)
{
    validate_bits(seed, "seed");
    require_valid_k(seed.size());
    return make_keystream(seed, num_bytes, [](const Bits& state) {
        return transform_canonical_unchecked(state);
    });
}

Bytes keystream_fast(const Bits& seed, std::size_t num_bytes)
{
    validate_bits(seed, "seed");
    require_valid_k(seed.size());
    const TransformPlan plan = make_transform_plan(seed.size());
    return make_keystream(seed, num_bytes, [&plan](const Bits& state) {
        return transform_fast_unchecked(state, plan);
    });
}

EncryptionResult encrypt_canonical(const Bytes& plain, const Bits& key_bits)
{
    EncryptionResult result;
    result.seed = derive_seed(plain, key_bits);
    const Bytes stream = keystream_canonical(result.seed, plain.size());
    result.cipher = xor_bytes(plain, stream);
    result.iterations = iteration_count(plain.size(), key_bits.size());
    return result;
}

EncryptionResult encrypt_fast(const Bytes& plain, const Bits& key_bits)
{
    EncryptionResult result;
    result.seed = derive_seed(plain, key_bits);
    const Bytes stream = keystream_fast(result.seed, plain.size());
    result.cipher = xor_bytes(plain, stream);
    result.iterations = iteration_count(plain.size(), key_bits.size());
    return result;
}

Bytes decrypt_canonical(const Bytes& cipher, const Bits& seed)
{
    return xor_bytes(cipher, keystream_canonical(seed, cipher.size()));
}

Bytes decrypt_fast(const Bytes& cipher, const Bits& seed)
{
    return xor_bytes(cipher, keystream_fast(seed, cipher.size()));
}

}  // namespace xormap_image
