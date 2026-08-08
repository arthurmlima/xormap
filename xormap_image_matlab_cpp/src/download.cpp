#include "xormap_image/download.hpp"
#include "xormap_image/image.hpp"

#include <curl/curl.h>

#include <cstdio>
#include <filesystem>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {

using xormap_image::SipiImageSpec;

std::once_flag curl_initialization;

void initialize_curl()
{
    std::call_once(curl_initialization, []() {
        const CURLcode status = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (status != CURLE_OK) {
            throw std::runtime_error(std::string("curl_global_init failed: ") +
                                     curl_easy_strerror(status));
        }
    });
}

std::size_t write_bytes(char* data, std::size_t size, std::size_t count, void* context)
{
    if (count != 0 && size > std::numeric_limits<std::size_t>::max() / count) {
        return 0;
    }
    return std::fwrite(data, 1, size * count, static_cast<std::FILE*>(context));
}

std::vector<SipiImageSpec> make_all_images()
{
    const char* const misc[] = {
        "5.1.09", "5.1.10", "5.1.11", "5.1.12", "5.1.13", "5.1.14",
        "5.2.08", "5.2.09", "5.2.10", "5.3.01", "5.3.02", "7.1.01",
        "7.1.02", "7.1.03", "7.1.04", "7.1.05", "7.1.06", "7.1.07",
        "7.1.08", "7.1.09", "7.1.10", "7.2.01", "boat.512", "gray21.512",
        "ruler.512"};
    const char* const aerials[] = {"3.2.25"};
    const char* const textures[] = {
        "1.1.01", "1.1.02", "1.1.03", "1.1.04", "1.1.05", "1.1.06",
        "1.1.07", "1.1.08", "1.1.09", "1.1.10", "1.1.11", "1.1.12",
        "1.1.13", "1.2.01", "1.2.02", "1.2.03", "1.2.04", "1.2.05",
        "1.2.06", "1.2.07", "1.2.08", "1.2.09", "1.2.10", "1.2.11",
        "1.2.12", "1.2.13", "1.3.01", "1.3.02", "1.3.03", "1.3.04",
        "1.3.05", "1.3.06", "1.3.07", "1.3.08", "1.3.09", "1.3.10",
        "1.3.11", "1.3.12", "1.3.13", "1.4.01", "1.4.02", "1.4.03",
        "1.4.04", "1.4.05", "1.4.06", "1.4.07", "1.4.08", "1.4.09",
        "1.4.10", "1.4.11", "1.4.12", "1.5.01", "1.5.02", "1.5.03",
        "1.5.04", "1.5.05", "1.5.06", "1.5.07", "texmos1.p512",
        "texmos2.p512", "texmos2.s512", "texmos3.p512", "texmos3b.p512",
        "texmos3.s512"};
    const char* const sequences[] = {
        "6.1.01", "6.1.02", "6.1.03", "6.1.04", "6.1.05", "6.1.06",
        "6.1.07", "6.1.08", "6.1.09", "6.1.10", "6.1.11", "6.1.12",
        "6.1.13", "6.1.14", "6.1.15", "6.1.16", "6.2.01", "6.2.02",
        "6.2.03", "6.2.04", "6.2.05", "6.2.06", "6.2.07", "6.2.08",
        "6.2.09", "6.2.10", "6.2.11", "6.2.12", "6.2.13", "6.2.14",
        "6.2.15", "6.2.16", "6.2.17", "6.2.18", "6.2.19", "6.2.20",
        "6.2.21", "6.2.22", "6.2.23", "6.2.24", "6.2.25", "6.2.26",
        "6.2.27", "6.2.28", "6.2.29", "6.2.30", "6.2.31", "6.2.32",
        "6.3.01", "6.3.02", "6.3.03", "6.3.04", "6.3.05", "6.3.06",
        "6.3.07", "6.3.08", "6.3.09", "6.3.10", "6.3.11", "motion01.512",
        "motion02.512", "motion03.512", "motion04.512", "motion05.512",
        "motion06.512", "motion07.512", "motion08.512", "motion09.512",
        "motion10.512"};

    std::vector<SipiImageSpec> result;
    result.reserve(159);
    const auto append = [&result](const char* volume, const auto& names) {
        for (const char* name : names) {
            result.push_back({volume, name});
        }
    };
    append("misc", misc);
    append("aerials", aerials);
    append("textures", textures);
    append("sequences", sequences);
    return result;
}

}  // namespace

namespace xormap_image {

const std::vector<SipiImageSpec>& basic_grayscale_images()
{
    static const std::vector<SipiImageSpec> images = {
        {"misc", "boat.512"}, {"misc", "5.1.09"}, {"misc", "7.1.01"}};
    return images;
}

const std::vector<SipiImageSpec>& all_grayscale_images()
{
    static const std::vector<SipiImageSpec> images = make_all_images();
    return images;
}

std::filesystem::path download_sipi_image(const SipiImageSpec& image,
                                          const std::filesystem::path& destination_directory,
                                          bool overwrite)
{
    if (image.volume.empty() || image.name.empty()) {
        throw std::invalid_argument("SIPI image volume and name must not be empty");
    }
    initialize_curl();
    std::filesystem::create_directories(destination_directory);

    const auto destination = destination_directory / (image.name + ".tiff");
    if (!overwrite && std::filesystem::is_regular_file(destination)) {
        // Never let a prior interrupted/manual download be treated as valid
        // merely because the destination name exists.
        (void)load_tiff_gray8(destination);
        return destination;
    }
    auto temporary = destination;
    temporary += ".part";

    std::FILE* file = std::fopen(temporary.string().c_str(), "wb");
    if (file == nullptr) {
        throw std::runtime_error("cannot open temporary download file: " + temporary.string());
    }

    CURL* handle = curl_easy_init();
    if (handle == nullptr) {
        std::fclose(file);
        std::filesystem::remove(temporary);
        throw std::runtime_error("curl_easy_init failed");
    }

    const std::string url = "https://sipi.usc.edu/database/download.php?vol=" +
                            image.volume + "&img=" + image.name;
    CURLcode option_status = CURLE_OK;
    const auto remember_option_error = [&option_status](CURLcode status) {
        if (option_status == CURLE_OK && status != CURLE_OK) {
            option_status = status;
        }
    };
    remember_option_error(curl_easy_setopt(handle, CURLOPT_URL, url.c_str()));
    remember_option_error(curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L));
    remember_option_error(curl_easy_setopt(handle, CURLOPT_FAILONERROR, 1L));
    remember_option_error(curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, 30L));
    remember_option_error(curl_easy_setopt(handle, CURLOPT_TIMEOUT, 300L));
    // The downloader runs easy handles on std::threads. Disabling libcurl's
    // process-signal timeout path is required for thread-safe resolver use.
    remember_option_error(curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L));
    remember_option_error(
        curl_easy_setopt(handle, CURLOPT_USERAGENT, "xormap-image-cpp/1.0"));
    remember_option_error(
        curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, &write_bytes));
    remember_option_error(curl_easy_setopt(handle, CURLOPT_WRITEDATA, file));
    if (option_status != CURLE_OK) {
        curl_easy_cleanup(handle);
        std::fclose(file);
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw std::runtime_error("cannot configure download for " + image.name +
                                 ": " + curl_easy_strerror(option_status));
    }

    const CURLcode status = curl_easy_perform(handle);
    curl_easy_cleanup(handle);
    const bool close_failed = std::fclose(file) != 0;
    if (status != CURLE_OK || close_failed) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        const std::string reason = status != CURLE_OK ? curl_easy_strerror(status)
                                                       : "failed to flush downloaded data";
        throw std::runtime_error("download failed for " + image.name + ": " + reason);
    }

    try {
        // Validate the temporary payload before publishing it under its final
        // name. A server-side HTML error with HTTP 200, a colour image, or an
        // unsupported TIFF therefore cannot poison subsequent non-overwrite
        // runs.
        (void)load_tiff_gray8(temporary);
    } catch (const std::exception& validation_error) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw std::runtime_error("downloaded file failed grayscale TIFF validation for " +
                                 image.name + ": " + validation_error.what());
    }

    std::error_code error;
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        std::filesystem::remove(temporary);
        throw std::runtime_error("cannot finalize download " + destination.string() + ": " +
                                 error.message());
    }
    return destination;
}

}  // namespace xormap_image
