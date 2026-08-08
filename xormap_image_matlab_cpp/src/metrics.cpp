#include "xormap_image/metrics.hpp"

#include "xormap_image/threefry.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

constexpr std::size_t kChecksumBytes = 4096U;
constexpr std::uint64_t kChecksumModulus = std::uint64_t{1} << 31U;

void require_nonempty(
    const std::vector<std::uint8_t>& values,
    const char* function_name) {
    if (values.empty()) {
        throw std::invalid_argument(std::string(function_name) +
                                    ": input must not be empty");
    }
}

void require_same_size(
    const std::vector<std::uint8_t>& first,
    const std::vector<std::uint8_t>& second,
    const char* function_name) {
    require_nonempty(first, function_name);
    require_nonempty(second, function_name);
    if (first.size() != second.size()) {
        throw std::invalid_argument(std::string(function_name) +
                                    ": inputs must have the same number of elements");
    }
}

std::size_t histogram_levels(int nbits, const char* function_name) {
    if (nbits < 1 || nbits > 8) {
        throw std::invalid_argument(std::string(function_name) +
                                    ": nbits must be in [1, 8] for byte data");
    }
    return std::size_t{1} << static_cast<unsigned int>(nbits);
}

void require_values_fit(
    const std::vector<std::uint8_t>& values,
    std::size_t levels,
    const char* function_name) {
    const auto first_bad = std::find_if(
        values.begin(), values.end(),
        [levels](std::uint8_t value) {
            return static_cast<std::size_t>(value) >= levels;
        });
    if (first_bad != values.end()) {
        throw std::invalid_argument(std::string(function_name) +
                                    ": a sample exceeds the range selected by nbits");
    }
}

void require_valid_max(
    const std::vector<std::uint8_t>& first,
    const std::vector<std::uint8_t>& second,
    double max_value,
    const char* function_name) {
    if (!std::isfinite(max_value) || max_value <= 0.0) {
        throw std::invalid_argument(std::string(function_name) +
                                    ": max_value must be finite and positive");
    }

    const auto exceeds_max = [max_value](std::uint8_t value) {
        return static_cast<double>(value) > max_value;
    };
    if (std::any_of(first.begin(), first.end(), exceeds_max) ||
        std::any_of(second.begin(), second.end(), exceeds_max)) {
        throw std::invalid_argument(std::string(function_name) +
                                    ": an input sample exceeds max_value");
    }
}

void require_shape(
    const std::vector<std::uint8_t>& pixels,
    std::size_t width,
    std::size_t height,
    const char* function_name) {
    require_nonempty(pixels, function_name);
    if (width == 0U || height == 0U) {
        throw std::invalid_argument(std::string(function_name) +
                                    ": width and height must be positive");
    }
    if (width > std::numeric_limits<std::size_t>::max() / height) {
        throw std::invalid_argument(std::string(function_name) +
                                    ": width * height overflows size_t");
    }
    if (width * height != pixels.size()) {
        throw std::invalid_argument(std::string(function_name) +
                                    ": width * height does not match the pixel count");
    }
}

double normal_upper_quantile(double alpha) {
    // Acklam's inverse-normal rational approximation, with the same
    // coefficients and branch boundaries as chi_square_uniformity.m.
    const double p = 1.0 - alpha;

    constexpr double a1 = -3.969683028665376e+01;
    constexpr double a2 = 2.209460984245205e+02;
    constexpr double a3 = -2.759285104469687e+02;
    constexpr double a4 = 1.383577518672690e+02;
    constexpr double a5 = -3.066479806614716e+01;
    constexpr double a6 = 2.506628277459239e+00;

    constexpr double b1 = -5.447609879822406e+01;
    constexpr double b2 = 1.615858368580409e+02;
    constexpr double b3 = -1.556989798598866e+02;
    constexpr double b4 = 6.680131188771972e+01;
    constexpr double b5 = -1.328068155288572e+01;

    constexpr double c1 = -7.784894002430293e-03;
    constexpr double c2 = -3.223964580411365e-01;
    constexpr double c3 = -2.400758277161838e+00;
    constexpr double c4 = -2.549732539343734e+00;
    constexpr double c5 = 4.374664141464968e+00;
    constexpr double c6 = 2.938163982698783e+00;

    constexpr double d1 = 7.784695709041462e-03;
    constexpr double d2 = 3.224671290700398e-01;
    constexpr double d3 = 2.445134137142996e+00;
    constexpr double d4 = 3.754408661907416e+00;

    constexpr double p_low = 0.02425;
    if (p < p_low) {
        const double q = std::sqrt(-2.0 * std::log(p));
        return (((((c1 * q + c2) * q + c3) * q + c4) * q + c5) * q + c6) /
               ((((d1 * q + d2) * q + d3) * q + d4) * q + 1.0);
    }
    if (p <= 1.0 - p_low) {
        const double q = p - 0.5;
        const double r = q * q;
        return (((((a1 * r + a2) * r + a3) * r + a4) * r + a5) * r + a6) * q /
               (((((b1 * r + b2) * r + b3) * r + b4) * r + b5) * r + 1.0);
    }

    const double q = std::sqrt(-2.0 * std::log(1.0 - p));
    return -(((((c1 * q + c2) * q + c3) * q + c4) * q + c5) * q + c6) /
           ((((d1 * q + d2) * q + d3) * q + d4) * q + 1.0);
}

double chi_square_critical(std::size_t degrees_of_freedom, double alpha) {
    const double dof = static_cast<double>(degrees_of_freedom);
    const double z = normal_upper_quantile(alpha);
    const double t = 2.0 / (9.0 * dof);
    return dof * std::pow(1.0 - t + z * std::sqrt(t), 3.0);
}

std::uint32_t checksum_impl(
    const std::vector<std::uint8_t>& first,
    const std::vector<std::uint8_t>* second) {
    const std::size_t count = std::min(first.size(), kChecksumBytes);
    std::uint64_t checksum = 0U;
    for (std::size_t index = 0; index < count; ++index) {
        std::uint8_t value = first[index];
        if (second != nullptr) {
            value = static_cast<std::uint8_t>(value ^ (*second)[index]);
        }
        checksum = (checksum +
                    static_cast<std::uint64_t>(value) *
                        static_cast<std::uint64_t>(index + 1U)) %
                   kChecksumModulus;
    }
    return static_cast<std::uint32_t>(checksum);
}

}  // namespace

namespace xormap_image {

MatlabTwister::MatlabTwister(std::uint32_t seed)
    // MATLAB maps rng(0, 'twister') to MT19937's historical default state.
    // Nonzero seeds use the ordinary single-word initialization.
    : generator_(seed == 0U ? 5489U : seed)
{
}

double MatlabTwister::next_uniform()
{
    // This is MT19937ar's genrand_res53, used by MATLAB's twister stream.
    // MATLAB rand returns values in the open interval (0, 1), so retry the
    // astronomically rare all-zero 53-bit draw.
    double value = 0.0;
    do {
        const std::uint32_t high27 =
            static_cast<std::uint32_t>(generator_()) >> 5U;
        const std::uint32_t low26 =
            static_cast<std::uint32_t>(generator_()) >> 6U;
        value = (static_cast<double>(high27) * 67108864.0 +
                 static_cast<double>(low26)) /
                9007199254740992.0;
    } while (value == 0.0);
    return value;
}

std::size_t MatlabTwister::randi(std::size_t inclusive_maximum)
{
    if (inclusive_maximum == 0U) {
        throw std::invalid_argument(
            "MatlabTwister::randi: inclusive maximum must be positive");
    }
    constexpr std::uintmax_t kMaximumExactInteger =
        UINT64_C(9007199254740992);
    if (static_cast<std::uintmax_t>(inclusive_maximum) >
        kMaximumExactInteger) {
        throw std::invalid_argument(
            "MatlabTwister::randi: inclusive maximum exceeds flintmax");
    }

    const double scaled =
        next_uniform() * static_cast<double>(inclusive_maximum);
    std::size_t zero_based = static_cast<std::size_t>(std::floor(scaled));
    // The res53 value is strictly below one. Keep the bound defensive in
    // case a platform's floating-point contraction rounds the product up.
    if (zero_based >= inclusive_maximum) {
        zero_based = inclusive_maximum - 1U;
    }
    return zero_based + 1U;
}

void MatlabTwister::discard_uniform(std::size_t count)
{
    for (std::size_t index = 0U; index < count; ++index) {
        static_cast<void>(next_uniform());
    }
}

double shannon_entropy(const std::vector<std::uint8_t>& image, int nbits) {
    constexpr const char* function_name = "shannon_entropy";
    require_nonempty(image, function_name);
    const std::size_t levels = histogram_levels(nbits, function_name);
    require_values_fit(image, levels, function_name);

    std::vector<std::size_t> counts(levels, 0U);
    for (const std::uint8_t value : image) {
        ++counts[value];
    }

    const double sample_count = static_cast<double>(image.size());
    double entropy = 0.0;
    for (const std::size_t count : counts) {
        if (count == 0U) {
            continue;
        }
        const double probability = static_cast<double>(count) / sample_count;
        entropy -= probability * std::log2(probability);
    }
    return entropy;
}

AdjacentDirection adjacent_direction_from_string(std::string_view direction) {
    if (direction == "horizontal") {
        return AdjacentDirection::Horizontal;
    }
    if (direction == "vertical") {
        return AdjacentDirection::Vertical;
    }
    if (direction == "diagonal") {
        return AdjacentDirection::Diagonal;
    }
    throw std::invalid_argument("adjacent_correlation: unknown direction '" +
                                std::string(direction) + "'");
}

template <typename Generator>
AdjacentCorrelationResult adjacent_correlation_with_generator(
    const std::vector<std::uint8_t>& pixels,
    std::size_t width,
    std::size_t height,
    AdjacentDirection direction,
    std::size_t num_samples,
    Generator& generator,
    bool collect_pairs) {
    constexpr const char* function_name = "adjacent_correlation";
    require_shape(pixels, width, height, function_name);
    if (num_samples == 0U) {
        throw std::invalid_argument(
            "adjacent_correlation: num_samples must be positive");
    }

    std::size_t row_count = height;
    std::size_t column_count = width;
    switch (direction) {
        case AdjacentDirection::Horizontal:
            if (width < 2U) {
                throw std::invalid_argument(
                    "adjacent_correlation: horizontal sampling requires width >= 2");
            }
            column_count = width - 1U;
            break;
        case AdjacentDirection::Vertical:
            if (height < 2U) {
                throw std::invalid_argument(
                    "adjacent_correlation: vertical sampling requires height >= 2");
            }
            row_count = height - 1U;
            break;
        case AdjacentDirection::Diagonal:
            if (width < 2U || height < 2U) {
                throw std::invalid_argument(
                    "adjacent_correlation: diagonal sampling requires width and height >= 2");
            }
            row_count = height - 1U;
            column_count = width - 1U;
            break;
        default:
            throw std::invalid_argument("adjacent_correlation: invalid direction value");
    }

    // MATLAB's implementation generates the complete row-index vector before
    // the column-index vector. Preserve that draw order for repeatability.
    std::vector<std::size_t> rows(num_samples);
    for (std::size_t& row : rows) {
        row = generator.randi(row_count) - 1U;
    }

    std::optional<AdjacentPixelPairs> pairs;
    if (collect_pairs) {
        pairs.emplace();
        pairs->x.reserve(num_samples);
        pairs->y.reserve(num_samples);
    }

    long double sum_x = 0.0L;
    long double sum_y = 0.0L;
    long double sum_x_squared = 0.0L;
    long double sum_y_squared = 0.0L;
    long double sum_xy = 0.0L;

    for (std::size_t sample = 0; sample < num_samples; ++sample) {
        const std::size_t row = rows[sample];
        const std::size_t column = generator.randi(column_count) - 1U;
        const std::size_t first_index = row * width + column;

        std::size_t second_index = first_index;
        switch (direction) {
            case AdjacentDirection::Horizontal:
                ++second_index;
                break;
            case AdjacentDirection::Vertical:
                second_index += width;
                break;
            case AdjacentDirection::Diagonal:
                second_index += width + 1U;
                break;
            default:
                throw std::logic_error("adjacent_correlation: unreachable direction");
        }

        const std::uint8_t x_byte = pixels[first_index];
        const std::uint8_t y_byte = pixels[second_index];
        const long double x = static_cast<long double>(x_byte);
        const long double y = static_cast<long double>(y_byte);
        sum_x += x;
        sum_y += y;
        sum_x_squared += x * x;
        sum_y_squared += y * y;
        sum_xy += x * y;

        if (pairs.has_value()) {
            pairs->x.push_back(x_byte);
            pairs->y.push_back(y_byte);
        }
    }

    const long double count = static_cast<long double>(num_samples);
    const long double covariance_term = count * sum_xy - sum_x * sum_y;
    const long double x_variance_term = count * sum_x_squared - sum_x * sum_x;
    const long double y_variance_term = count * sum_y_squared - sum_y * sum_y;

    double correlation = std::numeric_limits<double>::quiet_NaN();
    if (x_variance_term > 0.0L && y_variance_term > 0.0L) {
        const long double denominator =
            std::sqrt(x_variance_term * y_variance_term);
        correlation = static_cast<double>(covariance_term / denominator);
        // Round-off can push a mathematically bounded coefficient just past 1.
        correlation = std::max(-1.0, std::min(1.0, correlation));
    }

    return AdjacentCorrelationResult{correlation, std::move(pairs)};
}

AdjacentCorrelationResult adjacent_correlation(
    const std::vector<std::uint8_t>& pixels,
    std::size_t width,
    std::size_t height,
    AdjacentDirection direction,
    std::size_t num_samples,
    MatlabTwister& generator,
    bool collect_pairs) {
    return adjacent_correlation_with_generator(
        pixels, width, height, direction, num_samples, generator,
        collect_pairs);
}

AdjacentCorrelationResult adjacent_correlation(
    const std::vector<std::uint8_t>& pixels,
    std::size_t width,
    std::size_t height,
    AdjacentDirection direction,
    std::size_t num_samples,
    MatlabThreefry& generator,
    bool collect_pairs) {
    return adjacent_correlation_with_generator(
        pixels, width, height, direction, num_samples, generator,
        collect_pairs);
}

AdjacentCorrelationResult adjacent_correlation(
    const std::vector<std::uint8_t>& pixels,
    std::size_t width,
    std::size_t height,
    std::string_view direction,
    std::size_t num_samples,
    MatlabTwister& generator,
    bool collect_pairs) {
    return adjacent_correlation(
        pixels,
        width,
        height,
        adjacent_direction_from_string(direction),
        num_samples,
        generator,
        collect_pairs);
}

AdjacentCorrelationResult adjacent_correlation(
    const std::vector<std::uint8_t>& pixels,
    std::size_t width,
    std::size_t height,
    std::string_view direction,
    std::size_t num_samples,
    MatlabThreefry& generator,
    bool collect_pairs) {
    return adjacent_correlation(
        pixels,
        width,
        height,
        adjacent_direction_from_string(direction),
        num_samples,
        generator,
        collect_pairs);
}

AdjacentCorrelationResult adjacent_correlation(
    const std::vector<std::uint8_t>& pixels,
    std::size_t width,
    std::size_t height,
    AdjacentDirection direction,
    std::size_t num_samples,
    std::uint32_t seed,
    bool collect_pairs) {
    MatlabTwister generator(seed);
    return adjacent_correlation(
        pixels,
        width,
        height,
        direction,
        num_samples,
        generator,
        collect_pairs);
}

AdjacentCorrelationResult adjacent_correlation(
    const std::vector<std::uint8_t>& pixels,
    std::size_t width,
    std::size_t height,
    std::string_view direction,
    std::size_t num_samples,
    std::uint32_t seed,
    bool collect_pairs) {
    MatlabTwister generator(seed);
    return adjacent_correlation(
        pixels,
        width,
        height,
        adjacent_direction_from_string(direction),
        num_samples,
        generator,
        collect_pairs);
}

NpcrUaciResult npcr_uaci(
    const std::vector<std::uint8_t>& first,
    const std::vector<std::uint8_t>& second,
    double max_value) {
    constexpr const char* function_name = "npcr_uaci";
    require_same_size(first, second, function_name);
    require_valid_max(first, second, max_value, function_name);

    std::size_t changed = 0U;
    long double absolute_difference_sum = 0.0L;
    for (std::size_t index = 0; index < first.size(); ++index) {
        const int difference = static_cast<int>(first[index]) -
                               static_cast<int>(second[index]);
        if (difference != 0) {
            ++changed;
        }
        absolute_difference_sum += std::abs(difference);
    }

    const long double count = static_cast<long double>(first.size());
    const double npcr = static_cast<double>(
        100.0L * static_cast<long double>(changed) / count);
    const double uaci = static_cast<double>(
        100.0L * absolute_difference_sum /
        (count * static_cast<long double>(max_value)));
    return NpcrUaciResult{npcr, uaci};
}

NpcrUaciResult npcr_uaci_ideal(int nbits) {
    if (nbits < 1 || nbits > 63) {
        throw std::invalid_argument("npcr_uaci_ideal: nbits must be in [1, 63]");
    }
    const long double levels = std::ldexp(1.0L, nbits);
    const double npcr = static_cast<double>(100.0L * (levels - 1.0L) / levels);
    const double uaci = static_cast<double>(100.0L * (levels + 1.0L) /
                                            (3.0L * levels));
    return NpcrUaciResult{npcr, uaci};
}

ChiSquareResult chi_square_uniformity(
    const std::vector<std::uint8_t>& image,
    int nbits,
    double alpha) {
    constexpr const char* function_name = "chi_square_uniformity";
    require_nonempty(image, function_name);
    const std::size_t levels = histogram_levels(nbits, function_name);
    require_values_fit(image, levels, function_name);
    if (!std::isfinite(alpha) || alpha <= 0.0 || alpha >= 1.0) {
        throw std::invalid_argument(
            "chi_square_uniformity: alpha must be finite and in (0, 1)");
    }

    std::vector<std::size_t> counts(levels, 0U);
    for (const std::uint8_t value : image) {
        ++counts[value];
    }

    const double expected = static_cast<double>(image.size()) /
                            static_cast<double>(levels);
    double statistic = 0.0;
    for (const std::size_t count : counts) {
        const double difference = static_cast<double>(count) - expected;
        statistic += difference * difference / expected;
    }

    const std::size_t degrees_of_freedom = levels - 1U;
    const double critical_value = chi_square_critical(degrees_of_freedom, alpha);
    return ChiSquareResult{
        statistic,
        degrees_of_freedom,
        critical_value,
        statistic <= critical_value,
    };
}

PsnrResult psnr_db(
    const std::vector<std::uint8_t>& first,
    const std::vector<std::uint8_t>& second,
    double max_value) {
    constexpr const char* function_name = "psnr_db";
    require_same_size(first, second, function_name);
    require_valid_max(first, second, max_value, function_name);

    long double squared_error_sum = 0.0L;
    for (std::size_t index = 0; index < first.size(); ++index) {
        const long double difference =
            static_cast<long double>(first[index]) -
            static_cast<long double>(second[index]);
        squared_error_sum += difference * difference;
    }

    const double mse = static_cast<double>(
        squared_error_sum / static_cast<long double>(first.size()));
    if (mse == 0.0) {
        return PsnrResult{std::numeric_limits<double>::infinity(), 0.0};
    }

    const double result = 10.0 * std::log10(max_value * max_value / mse);
    return PsnrResult{result, mse};
}

std::uint32_t diff_checksum(const std::vector<std::uint8_t>& difference) {
    require_nonempty(difference, "diff_checksum");
    return checksum_impl(difference, nullptr);
}

std::uint32_t diff_checksum(
    const std::vector<std::uint8_t>& first,
    const std::vector<std::uint8_t>& second) {
    require_same_size(first, second, "diff_checksum");
    return checksum_impl(first, &second);
}

std::size_t first_differing_byte(const std::vector<std::uint8_t>& difference) {
    require_nonempty(difference, "first_differing_byte");
    const auto found = std::find_if(
        difference.begin(), difference.end(),
        [](std::uint8_t value) { return value != 0U; });
    if (found == difference.end()) {
        return 0U;
    }
    return static_cast<std::size_t>(std::distance(difference.begin(), found)) + 1U;
}

std::size_t first_differing_byte(
    const std::vector<std::uint8_t>& first,
    const std::vector<std::uint8_t>& second) {
    require_same_size(first, second, "first_differing_byte");
    const auto mismatch = std::mismatch(first.begin(), first.end(), second.begin());
    if (mismatch.first == first.end()) {
        return 0U;
    }
    return static_cast<std::size_t>(std::distance(first.begin(), mismatch.first)) + 1U;
}

}  // namespace xormap_image
