#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace xormap_image {

using Byte = std::uint8_t;
using Bytes = std::vector<Byte>;

// Bits are stored as bytes containing exactly 0 or 1.  Bit zero is the
// least-significant bit throughout this API, matching the MATLAB project.
using Bits = std::vector<Byte>;

// Throw std::invalid_argument when a value in bits is neither zero nor one.
void validate_bits(const Bits& bits, const char* argument_name = "bits");

// Expand/pack bytes in byte order, least-significant bit first within each
// byte.  bits_to_bytes requires a bit count divisible by eight.
Bits bytes_to_bits(const Bytes& bytes);
Bytes bits_to_bytes(const Bits& bits);

// Zero-based, inclusive windows used by the O(K) prefix-XOR transform.
struct TransformPlan {
    std::size_t k{};
    std::vector<std::size_t> left;
    std::vector<std::size_t> right;
    std::vector<std::size_t> odd_positions;
    std::vector<std::size_t> centers;
};

TransformPlan make_transform_plan(std::size_t k);

// Direct O(K^2) reference implementation and the equivalent O(K) form.
// Every transform input must contain K > 4 binary values.
Bits transform_canonical(const Bits& input);
Bits transform_fast(const Bits& input, const TransformPlan& plan);
Bits transform_fast(const Bits& input);

// SHA-256 and the MATLAB counter-mode expansion.  Each expansion block is
// SHA256(bytes || uint8(counter)), and digest bytes are expanded LSB-first.
Bytes sha256_bytes(const Bytes& bytes);
Bits hash_expand_bits(const Bytes& bytes, std::size_t k);

// Reproduce: rng(1729, 'twister'); rand(1, k) > 0.5
Bits secret_key(std::size_t k);

// Advance the XOR-map state before emitting each K-bit block, concatenate
// blocks, truncate to num_bytes * 8 bits, then pack LSB-first.
Bytes keystream_canonical(const Bits& seed, std::size_t num_bytes);
Bytes keystream_fast(const Bits& seed, std::size_t num_bytes);

struct EncryptionResult {
    Bytes cipher;
    Bits seed;
    std::size_t iterations{};
};

// The byte vectors are interpreted in row-major image order.  Shape metadata
// is deliberately outside the cipher core.
EncryptionResult encrypt_canonical(const Bytes& plain, const Bits& key_bits);
EncryptionResult encrypt_fast(const Bytes& plain, const Bits& key_bits);
Bytes decrypt_canonical(const Bytes& cipher, const Bits& seed);
Bytes decrypt_fast(const Bytes& cipher, const Bits& seed);

}  // namespace xormap_image
