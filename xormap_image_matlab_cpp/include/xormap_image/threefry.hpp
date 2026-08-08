#pragma once

#include "xormap_image/core.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace xormap_image {

// MATLAB's threefry4x64_20 stream as configured by rng(seed, "threefry").
// MATLAB workers use this generator by default, so an unqualified rng(seed)
// inside parfor retains Threefry rather than switching to the client-side
// Mersenne Twister. The class is a copyable value to keep worker tasks fully
// independent of native C++ scheduling.
class MatlabThreefry {
public:
    using Block = std::array<std::uint64_t, 4>;

    explicit MatlabThreefry(std::uint32_t seed = 0U);

    [[nodiscard]] double next_uniform();
    [[nodiscard]] std::size_t randi(std::size_t inclusive_maximum);
    void discard_uniform(std::size_t count);

    // Standard Random123 Threefry4x64 with 20 rounds. Exposed to make the
    // underlying algorithm independently testable against its published KAT.
    [[nodiscard]] static Block generate_block(
        const Block& counter,
        const Block& key);

private:
    Block counter_{};
    Block key_{};
    Block buffer_{};
    std::size_t buffer_index_ = buffer_.size();

    void refill();
    void increment_counter();
};

// secret_key.m called on a MATLAB worker snapshots its Threefry stream,
// executes rng(1729), generates K comparisons against 0.5, then restores it.
// A fresh local stream reproduces the generated key without mutable globals.
[[nodiscard]] Bits worker_secret_key(std::size_t k);

}  // namespace xormap_image
