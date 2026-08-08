#include "xormap_image/download.hpp"
#include "xormap_image/image.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>

namespace {

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
        path_ = parent / ("xormap-download-tests-" +
                          std::to_string(timestamp) + "-" +
                          std::to_string(sequence));
        if (!std::filesystem::create_directory(path_)) {
            throw std::runtime_error("could not create temporary download test directory");
        }
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

}  // namespace

TEST_CASE("the translated SIPI download inventories are complete",
          "[download][inventory]")
{
    const auto& basic = xormap_image::basic_grayscale_images();
    REQUIRE(basic.size() == 3U);
    REQUIRE(basic[0].volume == "misc");
    REQUIRE(basic[0].name == "boat.512");
    REQUIRE(basic[1].name == "5.1.09");
    REQUIRE(basic[2].name == "7.1.01");

    const auto& all = xormap_image::all_grayscale_images();
    REQUIRE(all.size() == 159U);
    std::set<std::string> names;
    std::map<std::string, std::size_t> volume_counts;
    for (const auto& image : all) {
        REQUIRE_FALSE(image.name.empty());
        REQUIRE_FALSE(image.volume.empty());
        REQUIRE(names.insert(image.name).second);
        ++volume_counts[image.volume];
    }
    REQUIRE(volume_counts ==
            std::map<std::string, std::size_t>{{"aerials", 1U},
                                                {"misc", 25U},
                                                {"sequences", 69U},
                                                {"textures", 64U}});
}

TEST_CASE("download validation rejects invalid specifications before transfer",
          "[download][errors]")
{
    TemporaryDirectory temporary;
    REQUIRE_THROWS_AS(
        xormap_image::download_sipi_image({"", "boat.512"}, temporary.path()),
        std::invalid_argument);
    REQUIRE_THROWS_AS(
        xormap_image::download_sipi_image({"misc", ""}, temporary.path()),
        std::invalid_argument);
}

TEST_CASE("an existing download is accepted only when it is a valid gray TIFF",
          "[download][tiff]")
{
    TemporaryDirectory temporary;
    const auto valid_path = temporary.path() / "valid.tiff";
    const xormap_image::Image image(3U, 2U, {0U, 1U, 2U, 253U, 254U, 255U});
    xormap_image::write_tiff_gray8(valid_path, image);
    REQUIRE(xormap_image::download_sipi_image({"misc", "valid"},
                                               temporary.path(), false) ==
            valid_path);
    REQUIRE(xormap_image::load_tiff_gray8(valid_path) == image);

    const auto invalid_path = temporary.path() / "invalid.tiff";
    {
        std::ofstream invalid(invalid_path, std::ios::binary);
        invalid << "HTTP 200 but not a TIFF";
    }
    REQUIRE_THROWS_AS(
        xormap_image::download_sipi_image({"misc", "invalid"},
                                          temporary.path(), false),
        std::runtime_error);
    REQUIRE(std::filesystem::is_regular_file(invalid_path));
    REQUIRE_FALSE(std::filesystem::exists(invalid_path.string() + ".part"));
}
