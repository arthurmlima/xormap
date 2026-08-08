#include "xormap_image/threefry.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace {

constexpr std::uint64_t kThreefryParity = UINT64_C(0x1BD11BDAA9FC1A22);
constexpr std::array<std::array<unsigned int, 2>, 8> kRotations{{
    {{14U, 16U}},
    {{52U, 57U}},
    {{23U, 40U}},
    {{5U, 37U}},
    {{25U, 33U}},
    {{46U, 12U}},
    {{58U, 22U}},
    {{32U, 32U}},
}};

std::uint64_t rotate_left(std::uint64_t value, unsigned int distance)
{
    return (value << distance) | (value >> (64U - distance));
}

void mix(std::uint64_t& first, std::uint64_t& second, unsigned int rotation)
{
    first += second;
    second = rotate_left(second, rotation) ^ first;
}

}  // namespace

namespace xormap_image {

MatlabThreefry::MatlabThreefry(std::uint32_t seed)
{
    // MATLAB stores the counter as eight uint32 values. Each 64-bit word is
    // [seed+odd, seed+even], with each 32-bit addition wrapping separately.
    for (std::size_t index = 0U; index < counter_.size(); ++index) {
        const auto offset = static_cast<std::uint32_t>(2U * index);
        const std::uint32_t low = static_cast<std::uint32_t>(seed + offset);
        const std::uint32_t high =
            static_cast<std::uint32_t>(seed + offset + 1U);
        counter_[index] = (static_cast<std::uint64_t>(high) << 32U) |
                          static_cast<std::uint64_t>(low);
    }
}

MatlabThreefry::Block MatlabThreefry::generate_block(
    const Block& counter,
    const Block& key)
{
    std::array<std::uint64_t, 5> schedule{{
        key[0], key[1], key[2], key[3], kThreefryParity,
    }};
    schedule[4] ^= schedule[0] ^ schedule[1] ^ schedule[2] ^ schedule[3];

    Block state{{
        counter[0] + schedule[0],
        counter[1] + schedule[1],
        counter[2] + schedule[2],
        counter[3] + schedule[3],
    }};

    for (std::size_t round = 0U; round < 20U; ++round) {
        const auto& rotations = kRotations[round % kRotations.size()];
        if ((round & 1U) == 0U) {
            mix(state[0], state[1], rotations[0]);
            mix(state[2], state[3], rotations[1]);
        } else {
            mix(state[0], state[3], rotations[0]);
            mix(state[2], state[1], rotations[1]);
        }

        if ((round + 1U) % 4U == 0U) {
            const std::size_t injection = (round + 1U) / 4U;
            state[0] += schedule[injection % schedule.size()];
            state[1] += schedule[(injection + 1U) % schedule.size()];
            state[2] += schedule[(injection + 2U) % schedule.size()];
            state[3] += schedule[(injection + 3U) % schedule.size()] +
                        static_cast<std::uint64_t>(injection);
        }
    }
    return state;
}

void MatlabThreefry::increment_counter()
{
    for (std::uint64_t& word : counter_) {
        ++word;
        if (word != 0U) {
            return;
        }
    }
    throw std::overflow_error("MatlabThreefry counter exhausted");
}

void MatlabThreefry::refill()
{
    buffer_ = generate_block(counter_, key_);
    increment_counter();
    buffer_index_ = 0U;
}

double MatlabThreefry::next_uniform()
{
    if (buffer_index_ == buffer_.size()) {
        refill();
    }
    const std::uint64_t raw = buffer_[buffer_index_++];

    // MATLAB FullPrecision mode discards 11 low bits, then uses the largest
    // representable double below 2^-53 as both scale and offset. Keep the
    // multiplication and addition as distinct rounded operations: combining
    // them into an FMA changes a small fraction of output bit patterns.
    constexpr double scale = 0x1.fffffffffffffp-54;
    volatile double scaled = static_cast<double>(raw >> 11U) * scale;
    return scaled + scale;
}

std::size_t MatlabThreefry::randi(std::size_t inclusive_maximum)
{
    if (inclusive_maximum == 0U) {
        throw std::invalid_argument(
            "MatlabThreefry::randi: inclusive maximum must be positive");
    }
    constexpr std::uintmax_t kMaximumExactInteger =
        UINT64_C(9007199254740992);
    if (static_cast<std::uintmax_t>(inclusive_maximum) >
        kMaximumExactInteger) {
        throw std::invalid_argument(
            "MatlabThreefry::randi: inclusive maximum exceeds flintmax");
    }

    const double scaled =
        next_uniform() * static_cast<double>(inclusive_maximum);
    std::size_t zero_based = static_cast<std::size_t>(std::floor(scaled));
    if (zero_based >= inclusive_maximum) {
        zero_based = inclusive_maximum - 1U;
    }
    return zero_based + 1U;
}

void MatlabThreefry::discard_uniform(std::size_t count)
{
    for (std::size_t index = 0U; index < count; ++index) {
        static_cast<void>(next_uniform());
    }
}

Bits worker_secret_key(std::size_t k)
{
    MatlabThreefry generator(1729U);
    Bits key(k, Byte{0});
    for (Byte& bit : key) {
        bit = static_cast<Byte>(generator.next_uniform() > 0.5 ? 1U : 0U);
    }
    return key;
}

}  // namespace xormap_image
