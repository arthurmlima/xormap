#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <string_view>
#include <vector>

namespace xormap_image {

class MatlabThreefry;

enum class AdjacentDirection {
    Horizontal,
    Vertical,
    Diagonal,
};

struct AdjacentPixelPairs {
    std::vector<std::uint8_t> x;
    std::vector<std::uint8_t> y;
};

struct AdjacentCorrelationResult {
    double correlation;
    std::optional<AdjacentPixelPairs> pairs;
};

// MATLAB's rng(seed, 'twister') stream. MATLAB constructs each rand value
// from the high 27 and 26 bits of consecutive MT19937ar outputs (the
// genrand_res53 mapping), and randi(n) is floor(rand*n)+1. Keeping this as
// an explicit, copyable value lets parallel drivers snapshot the original
// serial MATLAB stream at task boundaries without sharing mutable RNG state.
class MatlabTwister {
public:
    explicit MatlabTwister(std::uint32_t seed = 5489U);

    [[nodiscard]] double next_uniform();
    [[nodiscard]] std::size_t randi(std::size_t inclusive_maximum);
    void discard_uniform(std::size_t count);

private:
    std::mt19937 generator_;
};

struct NpcrUaciResult {
    double npcr_percent;
    double uaci_percent;
};

struct ChiSquareResult {
    double statistic;
    std::size_t degrees_of_freedom;
    double critical_value;
    bool passes;
};

struct PsnrResult {
    double psnr_db;
    double mse;
};

// Histogram entropy in bits. nbits must be in [1, 8], and every sample
// must fit in the requested number of bits.
double shannon_entropy(const std::vector<std::uint8_t>& image, int nbits = 8);

AdjacentDirection adjacent_direction_from_string(std::string_view direction);

// Stateful overloads consume the supplied MATLAB-compatible stream. They are
// useful when several calls must match MATLAB's global rng call order.
AdjacentCorrelationResult adjacent_correlation(
    const std::vector<std::uint8_t>& pixels,
    std::size_t width,
    std::size_t height,
    AdjacentDirection direction,
    std::size_t num_samples,
    MatlabTwister& generator,
    bool collect_pairs = false);

// Stateful Threefry overloads reproduce rng(seed) calls made inside MATLAB
// workers, whose default generator is Threefry rather than Twister.
AdjacentCorrelationResult adjacent_correlation(
    const std::vector<std::uint8_t>& pixels,
    std::size_t width,
    std::size_t height,
    AdjacentDirection direction,
    std::size_t num_samples,
    MatlabThreefry& generator,
    bool collect_pairs = false);

AdjacentCorrelationResult adjacent_correlation(
    const std::vector<std::uint8_t>& pixels,
    std::size_t width,
    std::size_t height,
    std::string_view direction,
    std::size_t num_samples,
    MatlabTwister& generator,
    bool collect_pairs = false);

AdjacentCorrelationResult adjacent_correlation(
    const std::vector<std::uint8_t>& pixels,
    std::size_t width,
    std::size_t height,
    std::string_view direction,
    std::size_t num_samples,
    MatlabThreefry& generator,
    bool collect_pairs = false);

// Seeded overloads reproduce a fresh rng(seed, 'twister') MATLAB stream on
// every call, so they never consume or modify global state.
AdjacentCorrelationResult adjacent_correlation(
    const std::vector<std::uint8_t>& pixels,
    std::size_t width,
    std::size_t height,
    AdjacentDirection direction,
    std::size_t num_samples,
    std::uint32_t seed = 5489U,
    bool collect_pairs = false);

AdjacentCorrelationResult adjacent_correlation(
    const std::vector<std::uint8_t>& pixels,
    std::size_t width,
    std::size_t height,
    std::string_view direction,
    std::size_t num_samples,
    std::uint32_t seed = 5489U,
    bool collect_pairs = false);

NpcrUaciResult npcr_uaci(
    const std::vector<std::uint8_t>& first,
    const std::vector<std::uint8_t>& second,
    double max_value = 255.0);

NpcrUaciResult npcr_uaci_ideal(int nbits = 8);

ChiSquareResult chi_square_uniformity(
    const std::vector<std::uint8_t>& image,
    int nbits = 8,
    double alpha = 0.05);

PsnrResult psnr_db(
    const std::vector<std::uint8_t>& first,
    const std::vector<std::uint8_t>& second,
    double max_value = 255.0);

// The checksum matches analysis_gray.m: the 1-based, position-weighted sum
// of at most the first 4096 row-major difference bytes, modulo 2^31.
std::uint32_t diff_checksum(const std::vector<std::uint8_t>& difference);

// Convenience overload: checksums the bytewise XOR difference directly.
std::uint32_t diff_checksum(
    const std::vector<std::uint8_t>& first,
    const std::vector<std::uint8_t>& second);

// Returns a MATLAB-compatible 1-based position, or zero when no byte differs.
std::size_t first_differing_byte(const std::vector<std::uint8_t>& difference);

std::size_t first_differing_byte(
    const std::vector<std::uint8_t>& first,
    const std::vector<std::uint8_t>& second);

}  // namespace xormap_image
