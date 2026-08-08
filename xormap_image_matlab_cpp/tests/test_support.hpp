#ifndef XORMAP_IMAGE_TEST_SUPPORT_HPP
#define XORMAP_IMAGE_TEST_SUPPORT_HPP

#include "xormap_image/image.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace test_support {

class TemporaryDirectory {
public:
    TemporaryDirectory()
    {
        static std::atomic<unsigned long long> serial{0U};
        const auto timestamp = std::chrono::steady_clock::now()
                                   .time_since_epoch()
                                   .count();
        const auto sequence = serial.fetch_add(1U, std::memory_order_relaxed);
        const std::filesystem::path parent =
            std::filesystem::temp_directory_path();
        for (unsigned int attempt = 0U; attempt < 100U; ++attempt) {
            const std::string name =
                "xormap-native-integration-" + std::to_string(timestamp) +
                "-" + std::to_string(sequence) + "-" +
                std::to_string(attempt);
            std::error_code error;
            if (std::filesystem::create_directory(parent / name, error)) {
                path_ = parent / name;
                return;
            }
            if (error) {
                throw std::runtime_error(
                    "could not create temporary test directory: " +
                    error.message());
            }
        }
        throw std::runtime_error(
            "could not allocate a unique temporary test directory");
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

inline void write_text(const std::filesystem::path& path,
                       std::string_view contents)
{
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("could not create test file '" +
                                 path.string() + "'");
    }
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!output) {
        throw std::runtime_error("could not write test file '" +
                                 path.string() + "'");
    }
}

[[nodiscard]] inline std::string read_text(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("could not read test file '" + path.string() +
                                 "'");
    }
    std::ostringstream contents;
    contents << input.rdbuf();
    if (input.bad()) {
        throw std::runtime_error("failed while reading test file '" +
                                 path.string() + "'");
    }
    return contents.str();
}

[[nodiscard]] inline std::vector<std::string> read_lines(
    const std::filesystem::path& path)
{
    std::istringstream input(read_text(path));
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(std::move(line));
    }
    return lines;
}

[[nodiscard]] inline std::vector<std::string> split_simple_csv(
    std::string_view line)
{
    if (line.find('"') != std::string_view::npos) {
        throw std::invalid_argument(
            "simple test CSV parser does not accept quoted fields");
    }
    std::vector<std::string> fields;
    std::size_t start = 0U;
    for (;;) {
        const std::size_t comma = line.find(',', start);
        if (comma == std::string_view::npos) {
            fields.emplace_back(line.substr(start));
            return fields;
        }
        fields.emplace_back(line.substr(start, comma - start));
        start = comma + 1U;
    }
}

[[nodiscard]] inline std::vector<std::vector<std::string>> read_simple_csv(
    const std::filesystem::path& path)
{
    std::vector<std::vector<std::string>> records;
    for (const std::string& line : read_lines(path)) {
        if (!line.empty()) {
            records.push_back(split_simple_csv(line));
        }
    }
    return records;
}

[[nodiscard]] inline bool has_pdf_signature(
    const std::filesystem::path& path)
{
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error ||
        std::filesystem::file_size(path, error) < 512U || error) {
        return false;
    }
    const std::string contents = read_text(path);
    return contents.rfind("%PDF-", 0U) == 0U &&
           contents.find("%%EOF") != std::string::npos;
}

[[nodiscard]] inline xormap_image::Image patterned_image(std::uint8_t salt)
{
    constexpr std::size_t width = 16U;
    constexpr std::size_t height = 16U;
    std::vector<std::uint8_t> pixels(width * height);
    for (std::size_t index = 0U; index < pixels.size(); ++index) {
        const std::size_t row = index / width;
        const std::size_t value = index * 37U + row * 19U +
                                  static_cast<std::size_t>(salt) * 53U;
        pixels[index] = static_cast<std::uint8_t>(value & 0xFFU);
    }
    return xormap_image::Image(width, height, std::move(pixels));
}

}  // namespace test_support

#endif  // XORMAP_IMAGE_TEST_SUPPORT_HPP
