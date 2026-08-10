// One-off export tool: encrypt a single grayscale image with the project's
// real cipher and write the plain/cipher TIFFs, pixel histograms, and
// adjacent-pixel correlation samples the paper's figures are built from.
// Reuses xormap_image's tested library functions directly (no
// reimplementation of the cipher).
//
// Usage: export_gray_figure <image.tiff> <K> <out_dir> <tag>

#include "xormap_image/core.hpp"
#include "xormap_image/image.hpp"
#include "xormap_image/metrics.hpp"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
using namespace xormap_image;

namespace {

void write_histogram_csv(const fs::path& path, const Bytes& plain,
                         const Bytes& cipher)
{
    std::array<std::size_t, 256> plain_counts{};
    std::array<std::size_t, 256> cipher_counts{};
    for (const auto b : plain) {
        ++plain_counts[b];
    }
    for (const auto b : cipher) {
        ++cipher_counts[b];
    }
    std::ofstream out(path);
    out << "level,plain_count,cipher_count\n";
    for (int level = 0; level < 256; ++level) {
        out << level << ',' << plain_counts[static_cast<std::size_t>(level)]
            << ',' << cipher_counts[static_cast<std::size_t>(level)] << '\n';
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
            std::cerr << "usage: export_gray_figure <image.tiff> <K> <out_dir> <tag>\n";
            return 2;
        }
        const fs::path image_path = argv[1];
        const std::size_t k = static_cast<std::size_t>(std::stoul(argv[2]));
        const fs::path out_dir = argv[3];
        const std::string tag = argv[4];
        fs::create_directories(out_dir);

        const Image plain = load_tiff_gray8(image_path);
        const Bits key = secret_key(k);
        const EncryptionResult encrypted = encrypt_fast(plain.pixels(), key);

        Image cipher_image(plain.width(), plain.height(), encrypted.cipher);
        write_tiff_gray8(out_dir / (tag + "_plain.tiff"), plain);
        write_tiff_gray8(out_dir / (tag + "_cipher.tiff"), cipher_image);

        write_histogram_csv(out_dir / (tag + "_histogram.csv"), plain.pixels(),
                            encrypted.cipher);

        constexpr std::size_t kSamples = 2000;
        constexpr std::uint32_t kSeed = 1;
        const auto plain_corr = adjacent_correlation(
            plain.pixels(), plain.width(), plain.height(),
            AdjacentDirection::Horizontal, kSamples, kSeed, true);
        const auto cipher_corr = adjacent_correlation(
            encrypted.cipher, plain.width(), plain.height(),
            AdjacentDirection::Horizontal, kSamples, kSeed, true);
        write_correlation_csv(out_dir / (tag + "_correlation.csv"),
                              *plain_corr.pairs, *cipher_corr.pairs);

        std::cout << "wrote " << tag << "_{plain,cipher}.tiff, "
                  << tag << "_histogram.csv, " << tag << "_correlation.csv to "
                  << out_dir << '\n'
                  << "plain entropy=" << shannon_entropy(plain.pixels())
                  << " cipher entropy=" << shannon_entropy(encrypted.cipher)
                  << " plain corrH=" << plain_corr.correlation
                  << " cipher corrH=" << cipher_corr.correlation << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
