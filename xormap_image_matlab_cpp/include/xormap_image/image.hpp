#ifndef XORMAP_IMAGE_IMAGE_HPP
#define XORMAP_IMAGE_IMAGE_HPP

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace xormap_image {

// An 8-bit, single-channel image. Pixels are stored a row at a time from the
// top-left corner; at(x, y) addresses column x in row y.
class Image {
public:
    Image() noexcept = default;
    Image(std::size_t width, std::size_t height);
    Image(std::size_t width, std::size_t height,
          std::vector<std::uint8_t> pixels);

    [[nodiscard]] std::size_t width() const noexcept { return width_; }
    [[nodiscard]] std::size_t height() const noexcept { return height_; }
    [[nodiscard]] std::size_t size() const noexcept { return pixels_.size(); }
    [[nodiscard]] bool empty() const noexcept { return pixels_.empty(); }

    [[nodiscard]] const std::vector<std::uint8_t>& pixels() const noexcept
    {
        return pixels_;
    }
    [[nodiscard]] const std::uint8_t* data() const noexcept
    {
        return pixels_.data();
    }
    [[nodiscard]] std::uint8_t* data() noexcept { return pixels_.data(); }

    [[nodiscard]] const std::uint8_t& at(std::size_t x,
                                         std::size_t y) const;
    [[nodiscard]] std::uint8_t& at(std::size_t x, std::size_t y);

    // Throws if this is the default-constructed empty image. All other Image
    // objects preserve this invariant by construction.
    void validate() const;

    friend bool operator==(const Image& lhs, const Image& rhs) noexcept;
    friend bool operator!=(const Image& lhs, const Image& rhs) noexcept
    {
        return !(lhs == rhs);
    }

private:
    std::size_t width_ = 0;
    std::size_t height_ = 0;
    std::vector<std::uint8_t> pixels_;
};

// Reads/writes one-directory, one-sample, unsigned 8-bit grayscale TIFFs.
// Loading normalizes every TIFF orientation to top-left row-major order and
// converts MINISWHITE samples to the Image convention (zero is black).
[[nodiscard]] Image load_tiff_gray8(const std::filesystem::path& path);
void write_tiff_gray8(const std::filesystem::path& path, const Image& image);

struct ManifestEntry {
    std::string filename;
    std::string volume;
    std::string name;
    std::size_t height = 0;
    std::size_t width = 0;
    std::size_t channels = 0;
    std::filesystem::path path;
};

// Parses the manifest produced by download_images_all_gray.m. If
// image_directory is empty, filenames are resolved relative to the manifest.
[[nodiscard]] std::vector<ManifestEntry> read_gray_manifest(
    const std::filesystem::path& manifest_path,
    const std::filesystem::path& image_directory = {});

// RFC 4180 field escaping for result/manifest writers.
[[nodiscard]] std::string csv_escape(std::string_view field);

}  // namespace xormap_image

#endif  // XORMAP_IMAGE_IMAGE_HPP
