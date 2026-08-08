#ifndef XORMAP_IMAGE_DOWNLOAD_HPP
#define XORMAP_IMAGE_DOWNLOAD_HPP

#include <filesystem>
#include <string>
#include <vector>

namespace xormap_image {

struct SipiImageSpec {
    std::string volume;
    std::string name;
};

const std::vector<SipiImageSpec>& basic_grayscale_images();
const std::vector<SipiImageSpec>& all_grayscale_images();

// Downloads one USC-SIPI image to <destination_directory>/<name>.tiff.
// Existing files are retained unless overwrite is true. The transfer first
// lands in a sibling .part file and is renamed only after a successful HTTP
// response and native grayscale-TIFF validation, so an interrupted or invalid
// download cannot corrupt an existing corpus.
std::filesystem::path download_sipi_image(const SipiImageSpec& image,
                                          const std::filesystem::path& destination_directory,
                                          bool overwrite = false);

}  // namespace xormap_image

#endif
