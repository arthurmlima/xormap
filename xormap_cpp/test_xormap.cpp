#include "xormap.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

using Pair = std::pair<std::size_t, std::size_t>;
using Column = std::vector<Pair>;

// Literal translation of the MATLAB matrix construction. Keeping this less
// direct version in the test catches column selection or bit-order mistakes in
// the implementation.
xormap::Integer matlab_reference(const xormap::Integer& input, std::size_t k)
{
    std::vector<Column> a(2 * k - 2);  // Includes the initial empty column.

    for (std::size_t cl = 1; cl < 2 * k - 2; ++cl) {
        const std::size_t base_i = cl <= k - 1 ? k - cl : 1;
        const std::size_t base_j =
            cl <= k - 1 ? k : 2 * k - 1 - cl;

        for (std::size_t n = 0; n < k; ++n) {
            const std::size_t i = base_i + n;
            const std::size_t j = base_j - n;
            if (i >= j) {
                break;
            }
            a[cl].push_back({i, j});
        }
    }

    a.erase(a.begin());
    const std::size_t centre = k - 1;
    const std::size_t left = k / 2;
    const std::size_t right = k - left - 1;
    const std::size_t start = centre - left;
    const std::size_t end = centre + right;

    std::vector<Column> m(a.begin() + (start - 1), a.begin() + end);
    std::reverse(m.begin(), m.end());

    xormap::Integer result = 0;
    for (std::size_t matlab_column = k; matlab_column >= 1; --matlab_column) {
        bool value = false;
        for (const Pair& pair : m[matlab_column - 1]) {
            value ^= boost::multiprecision::bit_test(input, pair.first - 1);
            value ^= boost::multiprecision::bit_test(input, pair.second - 1);
        }
        if (value) {
            boost::multiprecision::bit_set(result, k - matlab_column);
        }
    }
    return result;
}

void test_known_k5_map()
{
    const std::vector<std::vector<std::size_t>> masks = {
        {2, 4}, {1, 4, 2, 3}, {0, 4, 1, 3}, {0, 3, 1, 2}, {0, 2}};

    for (std::size_t input_bit = 0; input_bit < 5; ++input_bit) {
        xormap::Integer input = 0;
        xormap::Integer expected = 0;
        boost::multiprecision::bit_set(input, input_bit);
        for (std::size_t output_bit = 0; output_bit < masks.size(); ++output_bit) {
            if (std::find(masks[output_bit].begin(), masks[output_bit].end(),
                          input_bit) != masks[output_bit].end()) {
                boost::multiprecision::bit_set(expected, output_bit);
            }
        }
        assert(xormap::transform(input, 5) == expected);
    }
}

void test_width(std::size_t k, std::uint64_t& random_state)
{
    // Every input basis bit checks the complete linear transform.
    for (std::size_t bit = 0; bit < k; ++bit) {
        xormap::Integer input = 0;
        boost::multiprecision::bit_set(input, bit);
        assert(xormap::transform(input, k) == matlab_reference(input, k));
    }

    for (std::size_t sample = 0; sample < 50; ++sample) {
        xormap::Integer input = 0;
        for (std::size_t bit = 0; bit < k; ++bit) {
            random_state ^= random_state << 7;
            random_state ^= random_state >> 9;
            random_state ^= random_state << 8;
            if ((random_state & 1U) != 0) {
                boost::multiprecision::bit_set(input, bit);
            }
        }
        assert(xormap::transform(input, k) == matlab_reference(input, k));
    }
}

void test_against_matlab_construction()
{
    std::uint64_t random_state = 0x9e3779b97f4a7c15ULL;
    for (std::size_t k = 5; k <= 129; ++k) {
        test_width(k, random_state);
    }
}

void test_invalid_inputs()
{
    bool threw = false;
    try {
        (void)xormap::transform(0, 4);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    assert(threw);

    threw = false;
    try {
        (void)xormap::transform(-1, 5);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    assert(threw);

    threw = false;
    try {
        (void)xormap::transform(32, 5);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    assert(threw);
}

}  // namespace

int main()
{
    test_known_k5_map();
    test_against_matlab_construction();
    test_invalid_inputs();
    std::cout << "All XOR-map tests passed.\n";
    return 0;
}
