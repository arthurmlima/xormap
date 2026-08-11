// One-off export tool: encrypt a single RGB888 image with the project's
// real cipher and write the plain/cipher TIFFs, per-channel pixel
// histograms, and adjacent-pixel correlation samples for each of R/G/B
// the paper's figures are built from. Reuses xormap_color/xormap_image's
// tested library functions directly (no reimplementation of the cipher).
//
// Usage: export_rgb888_figure <image.tiff> <K> <out_dir> <tag>

#include "xormap_color/common.hpp"
#include "xormap_image/core.hpp"
#include "xormap_image/metrics.hpp"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
using namespace xormap_image;
using namespace xormap_color;

namespace {

void write_channel_histogram_csv(
    const fs::path& path,
    const std::array<std::vector<std::uint8_t>, 3>& plain,
    const std::array<std::vector<std::uint8_t>, 3>& cipher)
{
    std::array<std::array<std::size_t, 256>, 3> plain_counts{};
    std::array<std::array<std::size_t, 256>, 3> cipher_counts{};
    for (int c = 0; c < 3; ++c) {
        for (const auto b : plain[static_cast<std::size_t>(c)]) {
            ++plain_counts[static_cast<std::size_t>(c)][b];
        }
        for (const auto b : cipher[static_cast<std::size_t>(c)]) {
            ++cipher_counts[static_cast<std::size_t>(c)][b];
        }
    }
    std::ofstream out(path);
    out << "level,plain_r,plain_g,plain_b,cipher_r,cipher_g,cipher_b\n";
    for (int level = 0; level < 256; ++level) {
        const auto lv = static_cast<std::size_t>(level);
        out << level << ',' << plain_counts[0][lv] << ',' << plain_counts[1][lv]
            << ',' << plain_counts[2][lv] << ',' << cipher_counts[0][lv] << ','
            << cipher_counts[1][lv] << ',' << cipher_counts[2][lv] << '\n';
    }
}

void write_correlation_csv(const fs::path& path,
                           const AdjacentPixelPairs& plain,
                           const AdjacentPixelPairs& cipher)
{
    std::ofstream out(path);
    out << "plain_x,plain_y,cipher_x,cipher_y\n";
    const std::size_t n = plain.x.size();
    for (std::size_t i = 0; i < n; ++i) {
        out << static_cast<int>(plain.x[i]) << ',' << static_cast<int>(plain.y[i])
            << ',' << static_cast<int>(cipher.x[i]) << ',' << static_cast<int>(cipher.y[i])
            << '\n';
    }
}

}  // namespace

int main(int argc, char** argv)
{
    try {
        if (argc != 5) {
            std::cerr << "usage: export_rgb888_figure <image.tiff> <K> <out_dir> <tag>\n";
            return 2;
        }
        const fs::path image_path = argv[1];
        const std::size_t k = static_cast<std::size_t>(std::stoul(argv[2]));
        const fs::path out_dir = argv[3];
        const std::string tag = argv[4];
        fs::create_directories(out_dir);

        const RgbImage plain = load_tiff_rgb8(image_path);
        const Words plain_words = pack_rgb888(plain);
        const Bits key = secret_key(k);
        const WordEncryptionResult encrypted =
            encrypt_words_fast(plain_words, 24U, key);
        const RgbImage cipher_image =
            unpack_rgb888(encrypted.cipher, plain.width, plain.height);

        write_tiff_rgb8(out_dir / (tag + "_plain.tiff"), plain);
        write_tiff_rgb8(out_dir / (tag + "_cipher.tiff"), cipher_image);

        const auto plain_channels = rgb888_channels(plain_words);
        const auto cipher_channels = rgb888_channels(encrypted.cipher);
        write_channel_histogram_csv(out_dir / (tag + "_histogram.csv"),
                                    plain_channels, cipher_channels);

        constexpr std::size_t kSamples = 2000;
        constexpr std::uint32_t kSeed = 1;
        constexpr std::array<char, 3> kChannelLetters{'r', 'g', 'b'};
        for (int c = 0; c < 3; ++c) {
            const auto cu = static_cast<std::size_t>(c);
            const auto plain_corr = adjacent_correlation(
                plain_channels[cu], plain.width, plain.height,
                AdjacentDirection::Horizontal, kSamples, kSeed, true);
            const auto cipher_corr = adjacent_correlation(
                cipher_channels[cu], plain.width, plain.height,
                AdjacentDirection::Horizontal, kSamples, kSeed, true);
            write_correlation_csv(
                out_dir / (tag + "_correlation_" + kChannelLetters[cu] + ".csv"),
                *plain_corr.pairs, *cipher_corr.pairs);
            std::cout << tag << " " << kChannelLetters[cu]
                      << " plain corrH=" << plain_corr.correlation
                      << " cipher corrH=" << cipher_corr.correlation << '\n';
        }

        std::cout << "wrote " << tag << "_{plain,cipher}.tiff, "
                  << tag << "_histogram.csv, and "
                  << tag << "_correlation_{r,g,b}.csv to " << out_dir << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
