#include "xormap_image/core.hpp"
#include "xormap_image/image.hpp"
#include "xormap_image/metrics.hpp"
#include "xormap_image/threefry.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory()
    {
        static std::atomic<unsigned long long> serial{0};
        const auto timestamp = std::chrono::steady_clock::now()
                                   .time_since_epoch()
                                   .count();
        const auto sequence = serial.fetch_add(1, std::memory_order_relaxed);
        const std::filesystem::path parent =
            std::filesystem::temp_directory_path();

        for (unsigned int attempt = 0; attempt < 100U; ++attempt) {
            const std::string name =
                "xormap-image-cpp-tests-" + std::to_string(timestamp) + "-" +
                std::to_string(sequence) + "-" + std::to_string(attempt);
            std::error_code error;
            if (std::filesystem::create_directory(parent / name, error)) {
                path_ = parent / name;
                return;
            }
            if (error) {
                throw std::runtime_error("could not create temporary test directory: " +
                                         error.message());
            }
        }
        throw std::runtime_error("could not allocate a unique temporary test directory");
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    ~TemporaryDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};

void write_text_file(const std::filesystem::path& path, std::string_view text)
{
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("could not create test file " + path.string());
    }
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) {
        throw std::runtime_error("could not write test file " + path.string());
    }
}

std::string bytes_to_hex(const std::vector<std::uint8_t>& bytes)
{
    constexpr char digits[] = "0123456789ABCDEF";
    std::string result(bytes.size() * 2U, '0');
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        const auto value = static_cast<unsigned int>(bytes[index]);
        result[index * 2U] = digits[value >> 4U];
        result[index * 2U + 1U] = digits[value & 0x0FU];
    }
    return result;
}

std::optional<std::filesystem::path> find_corpus_image_directory()
{
    std::vector<std::filesystem::path> starting_points;
    std::error_code error;
    const auto source = std::filesystem::absolute(std::filesystem::path(__FILE__),
                                                   error);
    if (!error) {
        starting_points.push_back(source.parent_path());
    }
    error.clear();
    const auto working_directory = std::filesystem::current_path(error);
    if (!error) {
        starting_points.push_back(working_directory);
    }

    for (auto cursor : starting_points) {
        while (!cursor.empty()) {
            const auto candidate = cursor / "xormap_image_matlab_cpp" / "images";
            error.clear();
            if (std::filesystem::is_directory(candidate, error) && !error) {
                return candidate.lexically_normal();
            }
            const auto parent = cursor.parent_path();
            if (parent == cursor) {
                break;
            }
            cursor = parent;
        }
    }
    return std::nullopt;
}

}  // namespace

TEST_CASE("Image owns row-major grayscale pixels", "[image]")
{
    xormap_image::Image zeroes(3, 2);
    REQUIRE(zeroes.width() == 3U);
    REQUIRE(zeroes.height() == 2U);
    REQUIRE(zeroes.size() == 6U);
    REQUIRE_FALSE(zeroes.empty());
    REQUIRE(zeroes.pixels() == std::vector<std::uint8_t>(6, 0));
    REQUIRE_NOTHROW(zeroes.validate());

    zeroes.at(2, 1) = 91;
    zeroes.data()[0] = 17;
    const xormap_image::Image& read_only = zeroes;
    REQUIRE(read_only.at(0, 0) == 17U);
    REQUIRE(read_only.at(2, 1) == 91U);
    REQUIRE(read_only.pixels() ==
            std::vector<std::uint8_t>{17, 0, 0, 0, 0, 91});

    const xormap_image::Image same(3, 2, {17, 0, 0, 0, 0, 91});
    REQUIRE(zeroes == same);
    REQUIRE_FALSE(zeroes != same);
    REQUIRE(zeroes != xormap_image::Image(2, 3, {17, 0, 0, 0, 0, 91}));
    REQUIRE(zeroes != xormap_image::Image(3, 2, {17, 0, 0, 0, 0, 90}));
}

TEST_CASE("Image rejects invalid dimensions, pixel counts, and coordinates",
          "[image][errors]")
{
    const xormap_image::Image empty;
    REQUIRE(empty.width() == 0U);
    REQUIRE(empty.height() == 0U);
    REQUIRE(empty.size() == 0U);
    REQUIRE(empty.empty());
    REQUIRE_THROWS_AS(empty.validate(), std::invalid_argument);

    REQUIRE_THROWS_AS(xormap_image::Image(0, 1), std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::Image(1, 0), std::invalid_argument);
    REQUIRE_THROWS_AS(
        xormap_image::Image(std::numeric_limits<std::size_t>::max(), 2),
        std::length_error);
    REQUIRE_THROWS_AS(xormap_image::Image(2, 2, {1, 2, 3}),
                      std::invalid_argument);

    xormap_image::Image image(2, 2);
    const xormap_image::Image& read_only = image;
    REQUIRE_THROWS_AS(image.at(2, 0), std::out_of_range);
    REQUIRE_THROWS_AS(image.at(0, 2), std::out_of_range);
    REQUIRE_THROWS_AS(read_only.at(2, 2), std::out_of_range);
}

TEST_CASE("grayscale TIFF writing and loading round trip exactly", "[image][tiff]")
{
    TemporaryDirectory temporary;
    const auto path = temporary.path() / "roundtrip.tiff";
    const xormap_image::Image expected(
        4, 3,
        {0, 1, 2, 3, 17, 31, 63, 127, 128, 200, 254, 255});

    xormap_image::write_tiff_gray8(path, expected);
    REQUIRE(std::filesystem::is_regular_file(path));
    const auto actual = xormap_image::load_tiff_gray8(path);
    REQUIRE(actual == expected);
}

TEST_CASE("TIFF I/O reports invalid images and inaccessible paths",
          "[image][tiff][errors]")
{
    TemporaryDirectory temporary;
    const auto missing = temporary.path() / "does-not-exist.tiff";
    REQUIRE_THROWS_AS(xormap_image::load_tiff_gray8(missing),
                      std::runtime_error);
    REQUIRE_THROWS_AS(xormap_image::write_tiff_gray8(
                          temporary.path() / "empty.tiff", xormap_image::Image{}),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(xormap_image::write_tiff_gray8(
                          temporary.path() / "missing-parent" / "output.tiff",
                          xormap_image::Image(1, 1, {7})),
                      std::runtime_error);

    const auto invalid = temporary.path() / "not-a-tiff.tiff";
    write_text_file(invalid, "this is not a TIFF file");
    REQUIRE_THROWS_AS(xormap_image::load_tiff_gray8(invalid),
                      std::runtime_error);
}

TEST_CASE("manifest parsing handles MATLAB CSV quoting and path resolution",
          "[image][manifest]")
{
    TemporaryDirectory temporary;
    const auto manifest = temporary.path() / "manifest_gray.csv";
    write_text_file(
        manifest,
        "\xEF\xBB\xBF" "filename,volume,name,height,width,channels\r\n"
        "\"boat,copy.tiff\",\"vol\"\"ume\",\"Boat\ncopy\", 2 , 3 ,1\r\n"
        "\r\n");

    const auto entries = xormap_image::read_gray_manifest(manifest);
    REQUIRE(entries.size() == 1U);
    REQUIRE(entries[0].filename == "boat,copy.tiff");
    REQUIRE(entries[0].volume == "vol\"ume");
    REQUIRE(entries[0].name == "Boat\ncopy");
    REQUIRE(entries[0].height == 2U);
    REQUIRE(entries[0].width == 3U);
    REQUIRE(entries[0].channels == 1U);
    REQUIRE(entries[0].path ==
            (temporary.path() / "boat,copy.tiff").lexically_normal());

    const auto override_directory = temporary.path() / "images";
    REQUIRE(std::filesystem::create_directory(override_directory));
    const auto overridden =
        xormap_image::read_gray_manifest(manifest, override_directory);
    REQUIRE(overridden.size() == 1U);
    REQUIRE(overridden[0].path ==
            (override_directory / "boat,copy.tiff").lexically_normal());

    const auto header_only = temporary.path() / "empty_manifest.csv";
    write_text_file(header_only,
                    "filename,volume,name,height,width,channels\n");
    REQUIRE(xormap_image::read_gray_manifest(header_only).empty());
}

TEST_CASE("manifest parsing rejects malformed or non-grayscale CSV",
          "[image][manifest][errors]")
{
    TemporaryDirectory temporary;
    const auto absent = temporary.path() / "absent.csv";
    REQUIRE_THROWS_AS(xormap_image::read_gray_manifest(absent),
                      std::runtime_error);

    const std::vector<std::string> invalid_manifests = {
        "",
        "filename,volume,name,height,width\n",
        "filename,volume,name,height,width,other\n",
        "filename,volume,name,height,width,channels\na.tiff,v,n,1,1\n",
        "filename,volume,name,height,width,channels\na.tiff,v,n,0,1,1\n",
        "filename,volume,name,height,width,channels\na.tiff,v,n,nope,1,1\n",
        "filename,volume,name,height,width,channels\na.tiff,v,n,1,1,3\n",
        "filename,volume,name,height,width,channels\n,v,n,1,1,1\n",
        "filename,volume,name,height,width,channels\na.tiff,,n,1,1,1\n",
        "filename,volume,name,height,width,channels\na.tiff,v,,1,1,1\n",
        "filename,volume,name,height,width,channels\n\"unterminated,v,n,1,1,1\n",
        "filename,volume,name,height,width,channels\n\"a.tiff\"x,v,n,1,1,1\n",
        "filename,volume,name,height,width,channels\na\".tiff,v,n,1,1,1\n",
    };

    for (std::size_t index = 0; index < invalid_manifests.size(); ++index) {
        DYNAMIC_SECTION("invalid manifest " << index)
        {
            const auto path =
                temporary.path() / ("invalid-" + std::to_string(index) + ".csv");
            write_text_file(path, invalid_manifests[index]);
            REQUIRE_THROWS_AS(xormap_image::read_gray_manifest(path),
                              std::runtime_error);
        }
    }
}

TEST_CASE("CSV fields use RFC 4180 escaping", "[image][csv]")
{
    REQUIRE(xormap_image::csv_escape("") == "");
    REQUIRE(xormap_image::csv_escape("plain text") == "plain text");
    REQUIRE(xormap_image::csv_escape("with,comma") == "\"with,comma\"");
    REQUIRE(xormap_image::csv_escape("say \"hello\"") ==
            "\"say \"\"hello\"\"\"");
    REQUIRE(xormap_image::csv_escape("two\nlines") == "\"two\nlines\"");
    REQUIRE(xormap_image::csv_escape("carriage\rreturn") ==
            "\"carriage\rreturn\"");
}

TEST_CASE("real MATLAB grayscale fixtures match end-to-end golden results",
          "[image][matlab][integration]")
{
    const auto image_directory = find_corpus_image_directory();
    if (!image_directory.has_value()) {
        SKIP("xormap_image_matlab_cpp/images corpus is not available");
    }

    struct GoldenImage {
        std::string_view filename;
        std::size_t width;
        std::size_t height;
        std::size_t iterations;
        std::string_view plain_sha256;
        std::string_view cipher_sha256;
        double plain_entropy;
        double cipher_entropy;
        double npcr;
        double uaci;
    };

    const GoldenImage goldens[] = {
        {"boat.512.tiff", 512, 512, 4096,
         "B292548C463580074F3032FDECF2C1873114B797D0827A1E52E9EF37C99989F7",
         "B38368951A458B490C79BF41BD0ADE2AD95512CA33BB368F6B0EE70FA1D16AE1",
         7.19137021806924, 7.99938439993455, 99.609375,
         33.4909117455576},
        {"5.1.09.tiff", 256, 256, 1024,
         "C132DD7B0C65409CB3151D344FECAE5DF4EF95549130BDC61428DE5BF8E6E34C",
         "77CE8763B8597AA2ED81A5865B098CA8E8DF6FE005F44A3F951E40DA8BFED602",
         6.70931233596664, 7.9972683505147, 99.5742797851562,
         33.3923160328585},
        {"7.1.01.tiff", 512, 512, 4096,
         "4F2DACE2F4135C693EC965B2DD551B691FE93178E404D7745DA8A375E34CEDB2",
         "C6E8FFAE8597F27B6F5EF6135D0AF9FFDFA26ADA7ADC624967CB837EECDCE305",
         6.02741482100032, 7.99920158918419, 99.6070861816406,
         33.5379058239507},
    };

    const auto key = xormap_image::secret_key(512);
    for (const auto& golden : goldens) {
        DYNAMIC_SECTION(golden.filename)
        {
            const auto path = *image_directory / golden.filename;
            if (!std::filesystem::is_regular_file(path)) {
                SKIP("MATLAB fixture is not available: " << path.string());
            }

            const auto image = xormap_image::load_tiff_gray8(path);
            REQUIRE(image.width() == golden.width);
            REQUIRE(image.height() == golden.height);
            REQUIRE(image.size() == golden.width * golden.height);
            REQUIRE(bytes_to_hex(xormap_image::sha256_bytes(image.pixels())) ==
                    golden.plain_sha256);

            const auto encrypted =
                xormap_image::encrypt_fast(image.pixels(), key);
            REQUIRE(encrypted.iterations == golden.iterations);
            REQUIRE(bytes_to_hex(xormap_image::sha256_bytes(encrypted.cipher)) ==
                    golden.cipher_sha256);
            REQUIRE(xormap_image::decrypt_fast(encrypted.cipher,
                                               encrypted.seed) == image.pixels());
            REQUIRE(xormap_image::shannon_entropy(image.pixels()) ==
                    Catch::Approx(golden.plain_entropy).margin(1.0e-12));
            REQUIRE(xormap_image::shannon_entropy(encrypted.cipher) ==
                    Catch::Approx(golden.cipher_entropy).margin(1.0e-12));

            auto perturbed = image.pixels();
            const std::size_t center =
                (image.height() / 2U) * image.width() + image.width() / 2U;
            perturbed[center] =
                static_cast<std::uint8_t>(perturbed[center] ^ 1U);
            const auto perturbed_cipher =
                xormap_image::encrypt_fast(perturbed, key).cipher;
            const auto sensitivity =
                xormap_image::npcr_uaci(encrypted.cipher, perturbed_cipher);
            REQUIRE(sensitivity.npcr_percent ==
                    Catch::Approx(golden.npcr).margin(1.0e-12));
            REQUIRE(sensitivity.uaci_percent ==
                    Catch::Approx(golden.uaci).margin(1.0e-12));
        }
    }
}

TEST_CASE("serial MATLAB K=24 real-image vector matches exactly",
          "[image][matlab][integration]")
{
    const auto image_directory = find_corpus_image_directory();
    if (!image_directory.has_value()) {
        SKIP("xormap_image_matlab_cpp/images corpus is not available");
    }
    const auto path = *image_directory / "5.1.09.tiff";
    if (!std::filesystem::is_regular_file(path)) {
        SKIP("MATLAB fixture is not available: " << path.string());
    }

    const auto image = xormap_image::load_tiff_gray8(path);
    const auto encrypted =
        xormap_image::encrypt_fast(image.pixels(), xormap_image::secret_key(24));
    REQUIRE(encrypted.iterations == 21846U);
    REQUIRE(bytes_to_hex(xormap_image::sha256_bytes(encrypted.cipher)) ==
            "879C63936B26A3C5034E000254AADE32FD17FB860DCA9139967418588973A353");
    REQUIRE(xormap_image::shannon_entropy(encrypted.cipher) ==
            Catch::Approx(7.9965139592311).margin(1.0e-12));

    auto perturbed = image.pixels();
    const std::size_t center =
        (image.height() / 2U) * image.width() + image.width() / 2U;
    perturbed[center] = static_cast<std::uint8_t>(perturbed[center] ^ 1U);
    const auto changed = xormap_image::encrypt_fast(
        perturbed, xormap_image::secret_key(24));
    const auto sensitivity =
        xormap_image::npcr_uaci(encrypted.cipher, changed.cipher);
    REQUIRE(sensitivity.npcr_percent ==
            Catch::Approx(99.5285034179688).margin(1.0e-12));
    REQUIRE(sensitivity.uaci_percent ==
            Catch::Approx(33.4950585458793).margin(1.0e-12));
}

TEST_CASE("parallel MATLAB worker K=24 real-image vector matches exactly",
          "[image][matlab][threefry][parallel][integration]")
{
    const auto image_directory = find_corpus_image_directory();
    if (!image_directory.has_value()) {
        SKIP("xormap_image_matlab_cpp/images corpus is not available");
    }
    const auto path = *image_directory / "5.1.09.tiff";
    if (!std::filesystem::is_regular_file(path)) {
        SKIP("MATLAB fixture is not available: " << path.string());
    }

    const auto image = xormap_image::load_tiff_gray8(path);
    const auto encrypted = xormap_image::encrypt_fast(
        image.pixels(), xormap_image::worker_secret_key(24U));
    REQUIRE(encrypted.iterations == 21846U);
    REQUIRE(bytes_to_hex(xormap_image::sha256_bytes(encrypted.cipher)) ==
            "FD07773F3B26DBB59084F9A57D2742712A65A6AAE7402657EE6189EF45215A17");
    REQUIRE(xormap_image::shannon_entropy(encrypted.cipher) ==
            Catch::Approx(7.9963861755347168).margin(1.0e-12));

    xormap_image::MatlabThreefry sampling(1U);
    const auto horizontal = xormap_image::adjacent_correlation(
        encrypted.cipher, image.width(), image.height(),
        xormap_image::AdjacentDirection::Horizontal, 3000U, sampling);
    REQUIRE(horizontal.correlation ==
            Catch::Approx(0.022831289197619863).margin(1.0e-15));

    auto perturbed = image.pixels();
    const std::size_t center =
        (image.height() / 2U) * image.width() + image.width() / 2U;
    perturbed[center] = static_cast<std::uint8_t>(perturbed[center] ^ 1U);
    const auto changed = xormap_image::encrypt_fast(
        perturbed, xormap_image::worker_secret_key(24U));
    const auto sensitivity =
        xormap_image::npcr_uaci(encrypted.cipher, changed.cipher);
    REQUIRE(sensitivity.npcr_percent ==
            Catch::Approx(99.52850341796875).margin(1.0e-12));
    REQUIRE(sensitivity.uaci_percent ==
            Catch::Approx(33.714138853783702).margin(1.0e-12));
}
