#include "xormap.hpp"

#include <limits>
#include <stdexcept>

namespace {

bool get_bit(const xormap::Integer& value, std::size_t bit)
{
    return boost::multiprecision::bit_test(value, bit);
}

}  // namespace

namespace xormap {

Integer transform(const Integer& input, std::size_t k)
{
    if (k <= 4) {
        throw std::invalid_argument("K must be greater than 4");
    }
    if (k > std::numeric_limits<std::size_t>::max() / 2) {
        throw std::invalid_argument("K is too large");
    }
    if (input < 0) {
        throw std::invalid_argument("input must not be negative");
    }
    if ((input >> k) != 0) {
        throw std::invalid_argument("input does not fit in K bits");
    }

    Integer result = 0;

    // demo_chunks.m creates columns numbered 1 through 2*K-3 after its
    // initial empty column is removed. extract_3section.m selects K of those
    // columns. Genv_xormap.m then reverses them, and reverses the column
    // order again while assigning x_next. Those two reversals cancel, so
    // output bit b uses selected source column start_col + b.
    const std::size_t start_col = k - 1 - k / 2;  // MATLAB, one-based

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

        bool value = false;
        while (i < j) {
            value ^= get_bit(input, i - 1);
            value ^= get_bit(input, j - 1);
            ++i;
            --j;
        }
        if (value) {
            boost::multiprecision::bit_set(result, output_bit);
        }
    }

    return result;
}

}  // namespace xormap
