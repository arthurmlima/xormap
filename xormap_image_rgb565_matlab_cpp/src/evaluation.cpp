#include "xormap_rgb565/evaluation.hpp"

#include "xormap_color/common.hpp"
#include "xormap_image/image.hpp"
#include "xormap_image/parallel.hpp"
#include "xormap_image/plot.hpp"
#include "xormap_image/threefry.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <numeric>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace xormap_rgb565 {
namespace {

constexpr xormap_color::Word kWordMaximum = 0xFFFFU;
constexpr std::array<int, 3> kChannelBits{{5, 6, 5}};
constexpr std::array<const char*, 3> kChannelNames{{"R", "G", "B"}};

struct ConvertedEntry {
    std::string filename;
    std::string volume;
    std::string name;
    std::size_t height = 0;
    std::size_t width = 0;
    std::string source_tiff;
    std::filesystem::path path;
};

struct LoadedImage {
    ConvertedEntry entry;
    xormap_color::Words plain;
};

struct RunSummary {
    std::string name;
    std::array<double, 3> entropy_plain{};
    std::array<double, 3> entropy_cipher{};
    std::array<double, 3> corr_h_plain{};
    std::array<double, 3> corr_h_cipher{};
    std::array<double, 3> npcr_channel{};
    std::array<double, 3> uaci_channel{};
    double npcr_packed = 0.0;
    double uaci_packed = 0.0;
};

struct SweepRow {
    std::size_t image = 0;
    std::size_t k = 0;
    std::size_t height = 0;
    std::size_t width = 0;
    std::size_t pixels = 0;
    std::size_t iterations = 0;
    double pixels_per_iteration = 0.0;
    double normalized_entropy = 0.0;
    double correlation = 0.0;
    double npcr = 0.0;
    double uaci = 0.0;
    double seconds = 0.0;
};

[[nodiscard]] std::vector<std::string> split_csv(const std::string& line)
{
    std::vector<std::string> fields;
    std::stringstream input(line);
    std::string field;
    while (std::getline(input, field, ',')) {
        fields.push_back(field);
    }
    return fields;
}

[[nodiscard]] std::size_t parse_size(const std::string& value,
                                     const std::string& field)
{
    std::size_t parsed = 0;
    try {
        std::size_t used = 0;
        parsed = static_cast<std::size_t>(std::stoull(value, &used));
        if (used != value.size() || parsed == 0U) {
            throw std::invalid_argument("bad");
        }
    } catch (...) {
        throw std::runtime_error("converted manifest field '" + field +
                                 "' must be a positive integer");
    }
    return parsed;
}

[[nodiscard]] std::vector<ConvertedEntry> read_converted_manifest(
    const Options& options)
{
    std::ifstream input(options.paths.converted_manifest);
    if (!input) {
        throw std::runtime_error("converted RGB565 manifest not found: " +
            options.paths.converted_manifest.string() + ". Run convert first.");
    }
    std::string line;
    if (!std::getline(input, line) ||
        line != "filename,volume,name,height,width,source_tiff") {
        throw std::runtime_error("unexpected RGB565 converted manifest header");
    }
    std::vector<ConvertedEntry> entries;
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }
        const auto fields = split_csv(line);
        if (fields.size() != 6U) {
            throw std::runtime_error("malformed RGB565 converted manifest row");
        }
        entries.push_back({fields[0], fields[1], fields[2],
                           parse_size(fields[3], "height"),
                           parse_size(fields[4], "width"), fields[5],
                           (options.paths.converted_images / fields[0]).lexically_normal()});
    }
    if (entries.size() != 51U) {
        throw std::runtime_error("RGB565 converted manifest must contain all 51 RGB888 images");
    }
    return entries;
}

[[nodiscard]] std::vector<LoadedImage> load_converted(const Options& options)
{
    const auto entries = read_converted_manifest(options);
    std::vector<LoadedImage> images;
    images.reserve(entries.size());
    for (const auto& entry : entries) {
        std::size_t width = 0;
        std::size_t height = 0;
        auto words = xormap_color::read_rgb565_raw(entry.path, width, height);
        if (width != entry.width || height != entry.height) {
            throw std::runtime_error("RGB565 dimensions disagree with manifest for " +
                                     entry.name);
        }
        images.push_back({entry, std::move(words)});
    }
    return images;
}

[[nodiscard]] std::string display_name(const std::string& name,
                                       bool include_size)
{
    static const std::map<std::string, std::string> descriptions{
        {"4.2.03", "Mandrill"}, {"4.2.05", "Airplane F-16"},
        {"4.2.07", "Peppers"}};
    const auto found = descriptions.find(name);
    std::string value = name;
    if (found != descriptions.end()) {
        value += " (" + found->second;
        if (include_size) {
            value += ", 512x512";
        }
        value += ")";
    }
    return value;
}

[[nodiscard]] std::vector<std::size_t> selected_indices(
    const std::vector<LoadedImage>& images, bool all)
{
    if (all) {
        std::vector<std::size_t> indexes(images.size());
        std::iota(indexes.begin(), indexes.end(), 0U);
        return indexes;
    }
    const std::array<std::string, 3> selected{{"4.2.03", "4.2.05", "4.2.07"}};
    std::vector<std::size_t> indexes;
    for (const auto& name : selected) {
        const auto found = std::find_if(images.begin(), images.end(),
            [&](const LoadedImage& image) { return image.entry.name == name; });
        if (found == images.end()) {
            throw std::runtime_error("converted corpus is missing " + name);
        }
        indexes.push_back(static_cast<std::size_t>(std::distance(images.begin(), found)));
    }
    return indexes;
}

[[nodiscard]] xormap_image::Panel channel_histogram_panel(
    std::string title, const std::vector<std::uint8_t>& values,
    int nbits, xormap_image::Color color)
{
    const std::size_t levels = std::size_t{1} << nbits;
    std::vector<std::size_t> counts(levels, 0U);
    for (const std::uint8_t value : values) {
        ++counts[value];
    }
    xormap_image::Series bars;
    bars.label = "count";
    bars.style = xormap_image::SeriesStyle::Bars;
    bars.color = color;
    for (std::size_t level = 0; level < levels; ++level) {
        bars.points.push_back({static_cast<double>(level),
                               static_cast<double>(counts[level])});
    }
    xormap_image::Panel panel;
    panel.title = std::move(title);
    panel.x_label = "Channel value";
    panel.y_label = "Count";
    panel.series.push_back(std::move(bars));
    panel.show_legend = false;
    panel.x_range = xormap_image::AxisRange{0.0,
        static_cast<double>(levels - 1U)};
    return panel;
}

[[nodiscard]] xormap_image::Panel channel_scatter_panel(
    std::string title, const xormap_image::AdjacentPixelPairs& pairs,
    int nbits, xormap_image::Color color)
{
    auto panel = xormap_image::make_adjacent_scatter_panel(
        std::move(title), pairs, color);
    const double maximum = static_cast<double>((std::size_t{1} << nbits) - 1U);
    panel.x_range = xormap_image::AxisRange{0.0, maximum};
    panel.y_range = xormap_image::AxisRange{0.0, maximum};
    panel.square_axes = true;
    panel.show_legend = false;
    return panel;
}

[[nodiscard]] xormap_image::Panel aggregate_panel(
    const std::string& title, const std::string& y_label,
    const std::vector<SweepRow>& rows, const std::vector<std::size_t>& ks,
    double SweepRow::*field, double ideal)
{
    xormap_image::Panel panel;
    panel.title = title;
    panel.x_label = "K (state bits)";
    panel.y_label = y_label;
    xormap_image::Series points;
    points.label = "individual images";
    points.style = xormap_image::SeriesStyle::Scatter;
    points.marker_radius = 1.0;
    points.color = {0.62, 0.62, 0.60, 0.22};
    for (const auto& row : rows) {
        points.points.push_back({static_cast<double>(row.k), row.*field});
    }
    panel.series.push_back(std::move(points));
    xormap_image::Series means;
    means.label = "mean";
    means.style = xormap_image::SeriesStyle::Line;
    means.line_width = 2.0;
    means.marker_radius = 2.7;
    means.color = {0.922, 0.408, 0.204, 1.0};
    for (const std::size_t k : ks) {
        long double sum = 0.0L;
        std::size_t count = 0;
        for (const auto& row : rows) {
            if (row.k == k) {
                sum += row.*field;
                ++count;
            }
        }
        means.points.push_back({static_cast<double>(k),
            static_cast<double>(sum / static_cast<long double>(count))});
    }
    panel.series.push_back(std::move(means));
    if (std::isfinite(ideal)) {
        panel.reference_lines.push_back({ideal, "ideal",
            {0.35, 0.35, 0.35, 0.8},
            xormap_image::ReferenceOrientation::Horizontal, 1.0});
    }
    panel.x_range = xormap_image::AxisRange{
        static_cast<double>(ks.front()) - 18.0,
        static_cast<double>(ks.back()) + 18.0};
    return panel;
}

void write_run_results(const Options& options,
                       const std::vector<RunSummary>& summaries)
{
    const auto packed_ideal = xormap_image::npcr_uaci_ideal(16);
    const auto five_ideal = xormap_image::npcr_uaci_ideal(5);
    const auto six_ideal = xormap_image::npcr_uaci_ideal(6);
    const auto path = options.paths.results / "results.md";
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("could not create " + path.string());
    }
    output << "## Entropy and packed RGB565 differential metrics\n\n"
           << "Packed ideal NPCR/UACI: " << std::fixed << std::setprecision(4)
           << packed_ideal.npcr_percent << "% / " << packed_ideal.uaci_percent
           << "%\n\n"
           << "| Image | R plain | R cipher | G plain | G cipher | B plain | B cipher | NPCR % | UACI % |\n"
           << "|---|---:|---:|---:|---:|---:|---:|---:|---:|\n";
    for (const auto& row : summaries) {
        output << "| " << row.name;
        for (std::size_t channel = 0; channel < 3U; ++channel) {
            output << " | " << row.entropy_plain[channel]
                   << " | " << row.entropy_cipher[channel];
        }
        output << " | " << row.npcr_packed << " | " << row.uaci_packed << " |\n";
    }
    output << "\n## Horizontal adjacent-pixel correlation\n\n"
           << "| Image | R plain | R cipher | G plain | G cipher | B plain | B cipher |\n"
           << "|---|---:|---:|---:|---:|---:|---:|\n";
    for (const auto& row : summaries) {
        output << "| " << row.name;
        for (std::size_t channel = 0; channel < 3U; ++channel) {
            output << " | " << std::showpos << row.corr_h_plain[channel]
                   << " | " << row.corr_h_cipher[channel] << std::noshowpos;
        }
        output << " |\n";
    }
    output << "\n## Per-channel NPCR/UACI\n\n"
           << "R/B ideal: " << five_ideal.npcr_percent << "% / "
           << five_ideal.uaci_percent << "%; G ideal: "
           << six_ideal.npcr_percent << "% / " << six_ideal.uaci_percent
           << "%\n\n"
           << "| Image | NPCR R | UACI R | NPCR G | UACI G | NPCR B | UACI B |\n"
           << "|---|---:|---:|---:|---:|---:|---:|\n";
    for (const auto& row : summaries) {
        output << "| " << row.name;
        for (std::size_t channel = 0; channel < 3U; ++channel) {
            output << " | " << row.npcr_channel[channel]
                   << " | " << row.uaci_channel[channel];
        }
        output << " |\n";
    }
}

}  // namespace

std::vector<std::size_t> k_values(std::size_t first, std::size_t step,
                                  std::size_t last)
{
    if (first <= 4U || step == 0U || last < first) {
        throw std::invalid_argument("invalid K range");
    }
    std::vector<std::size_t> values;
    for (std::size_t k = first; k <= last;) {
        values.push_back(k);
        if (last - k < step) {
            break;
        }
        k += step;
    }
    return values;
}

void verify(std::ostream& progress)
{
    const xormap_color::RgbImage fixture(
        2, 2, {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255});
    const auto packed = xormap_color::pack_rgb565(fixture);
    if (packed != xormap_color::Words{0xF800U, 0x07E0U, 0x001FU, 0xFFFFU}) {
        throw std::runtime_error("RGB888-to-RGB565 packing mismatch");
    }
    for (const std::size_t k : k_values(8U, 4U, 512U)) {
        xormap_image::Bits seed(k, 0U);
        for (std::size_t i = 0; i < k; ++i) {
            seed[i] = static_cast<std::uint8_t>(((i * 41U + k) >> 3U) & 1U);
        }
        if (xormap_color::keystream_words_fast(seed, 16U, 100U) !=
            xormap_color::keystream_words_canonical(seed, 16U, 100U)) {
            throw std::runtime_error("RGB565 fast/canonical stream mismatch at K=" +
                                     std::to_string(k));
        }
        const auto encrypted = xormap_color::encrypt_words_fast(
            packed, 16U, xormap_image::secret_key(k));
        if (xormap_color::decrypt_words_fast(
                encrypted.cipher, 16U, encrypted.seed) != packed) {
            throw std::runtime_error("RGB565 round-trip mismatch at K=" +
                                     std::to_string(k));
        }
    }
    progress << "All translated RGB565 tests passed for K=8:4:512.\n";
}

void convert_all(const Options& options, std::ostream& progress)
{
    const auto sources = xormap_color::read_color_manifest(
        options.paths.source_manifest, options.paths.source_images);
    if (sources.size() != 51U) {
        throw std::runtime_error("RGB888 source manifest must contain all 51 images");
    }
    struct Converted { std::size_t width; std::size_t height; std::string filename; };
    std::vector<Converted> converted(sources.size());
    std::filesystem::create_directories(options.paths.converted_images);
    xormap_image::parallel_for(sources.size(), options.workers,
        [&](std::size_t index) {
            const auto image = xormap_color::load_tiff_rgb8(sources[index].path);
            const auto words = xormap_color::pack_rgb565(image);
            const std::string filename = sources[index].name + ".rgb565";
            const auto destination = options.paths.converted_images / filename;
            if (options.overwrite || !std::filesystem::exists(destination)) {
                xormap_color::write_rgb565_raw(destination, words,
                                               image.width, image.height);
            } else {
                std::size_t width = 0;
                std::size_t height = 0;
                const auto existing = xormap_color::read_rgb565_raw(
                    destination, width, height);
                if (width != image.width || height != image.height ||
                    existing != words) {
                    throw std::runtime_error("existing RGB565 conversion is stale: " +
                                             destination.string());
                }
            }
            converted[index] = {image.width, image.height, filename};
        });
    std::ofstream manifest(options.paths.converted_manifest);
    if (!manifest) {
        throw std::runtime_error("could not create converted manifest");
    }
    manifest << "filename,volume,name,height,width,source_tiff\n";
    for (std::size_t i = 0; i < sources.size(); ++i) {
        manifest << converted[i].filename << ',' << sources[i].volume << ','
                 << sources[i].name << ',' << converted[i].height << ','
                 << converted[i].width << ',' << sources[i].filename << '\n';
    }
    progress << "converted and verified all " << sources.size()
             << " RGB888 TIFFs as packed big-endian RGB565 files\n";
}

void run_all(const Options& options, std::ostream& progress)
{
    const auto images = load_converted(options);
    const auto indexes = selected_indices(images, options.all_images);
    std::filesystem::create_directories(options.paths.results);
    const xormap_image::Bits key = xormap_image::secret_key(512U);
    xormap_image::MatlabTwister rng(2026U);
    std::vector<RunSummary> summaries;
    summaries.reserve(indexes.size());
    for (const std::size_t image_index : indexes) {
        const auto& image = images[image_index];
        xormap_color::Words changed = image.plain;
        changed[(image.entry.height / 2U) * image.entry.width +
                image.entry.width / 2U] ^= 1U;
        const auto encrypted = xormap_color::encrypt_words_fast(image.plain, 16U, key);
        const auto cipher2 = xormap_color::encrypt_words_fast(changed, 16U, key).cipher;
        if (xormap_color::decrypt_words_fast(
                encrypted.cipher, 16U, encrypted.seed) != image.plain) {
            throw std::runtime_error("RGB565 run-all round-trip failure");
        }
        const auto plain_channels = xormap_color::rgb565_channels(image.plain);
        const auto cipher_channels = xormap_color::rgb565_channels(encrypted.cipher);
        const auto cipher2_channels = xormap_color::rgb565_channels(cipher2);
        RunSummary summary;
        summary.name = display_name(image.entry.name, true);
        std::array<std::array<double, 3>, 3> plain_corr{};
        std::array<std::array<double, 3>, 3> cipher_corr{};
        for (std::size_t channel = 0; channel < 3U; ++channel) {
            summary.entropy_plain[channel] = xormap_image::shannon_entropy(
                plain_channels[channel], kChannelBits[channel]);
            summary.entropy_cipher[channel] = xormap_image::shannon_entropy(
                cipher_channels[channel], kChannelBits[channel]);
            for (std::size_t direction = 0; direction < 3U; ++direction) {
                const auto enum_direction = static_cast<xormap_image::AdjacentDirection>(direction);
                plain_corr[channel][direction] = xormap_image::adjacent_correlation(
                    plain_channels[channel], image.entry.width, image.entry.height,
                    enum_direction, 5000U, rng).correlation;
            }
            for (std::size_t direction = 0; direction < 3U; ++direction) {
                const auto enum_direction = static_cast<xormap_image::AdjacentDirection>(direction);
                cipher_corr[channel][direction] = xormap_image::adjacent_correlation(
                    cipher_channels[channel], image.entry.width, image.entry.height,
                    enum_direction, 5000U, rng).correlation;
            }
            summary.corr_h_plain[channel] = plain_corr[channel][0];
            summary.corr_h_cipher[channel] = cipher_corr[channel][0];
            const auto channel_diff = xormap_image::npcr_uaci(
                cipher_channels[channel], cipher2_channels[channel],
                static_cast<double>((std::size_t{1} << kChannelBits[channel]) - 1U));
            summary.npcr_channel[channel] = channel_diff.npcr_percent;
            summary.uaci_channel[channel] = channel_diff.uaci_percent;
        }
        const auto packed_diff = xormap_color::npcr_uaci_words(
            encrypted.cipher, cipher2, kWordMaximum);
        summary.npcr_packed = packed_diff.npcr_percent;
        summary.uaci_packed = packed_diff.uaci_percent;

        std::vector<xormap_image::Panel> histogram_panels;
        const std::array<xormap_image::Color, 3> colors{{
            {0.85, 0.20, 0.20, 0.88}, {0.20, 0.60, 0.25, 0.88},
            {0.20, 0.35, 0.85, 0.88}}};
        for (std::size_t channel = 0; channel < 3U; ++channel) {
            histogram_panels.push_back(channel_histogram_panel(
                "Plain " + std::string(kChannelNames[channel]),
                plain_channels[channel], kChannelBits[channel], colors[channel]));
        }
        for (std::size_t channel = 0; channel < 3U; ++channel) {
            histogram_panels.push_back(channel_histogram_panel(
                "Cipher " + std::string(kChannelNames[channel]),
                cipher_channels[channel], kChannelBits[channel], colors[channel]));
        }
        const std::string tag = image.entry.name;
        xormap_image::write_plot_grid_pdf(
            options.paths.results / (tag + "_histogram.pdf"), summary.name,
            "RGB565 channel histograms: top plain, bottom cipher",
            histogram_panels, 3U);

        std::array<xormap_image::AdjacentPixelPairs, 3> plain_pairs;
        std::array<xormap_image::AdjacentPixelPairs, 3> cipher_pairs;
        for (std::size_t channel = 0; channel < 3U; ++channel) {
            plain_pairs[channel] = xormap_image::adjacent_correlation(
                plain_channels[channel], image.entry.width, image.entry.height,
                xormap_image::AdjacentDirection::Horizontal,
                options.scatter_samples, rng, true).pairs.value();
            cipher_pairs[channel] = xormap_image::adjacent_correlation(
                cipher_channels[channel], image.entry.width, image.entry.height,
                xormap_image::AdjacentDirection::Horizontal,
                options.scatter_samples, rng, true).pairs.value();
        }
        std::vector<xormap_image::Panel> scatter_panels;
        for (std::size_t channel = 0; channel < 3U; ++channel) {
            scatter_panels.push_back(channel_scatter_panel(
                "Plain " + std::string(kChannelNames[channel]),
                plain_pairs[channel],
                kChannelBits[channel], colors[channel]));
        }
        for (std::size_t channel = 0; channel < 3U; ++channel) {
            scatter_panels.push_back(channel_scatter_panel(
                "Cipher " + std::string(kChannelNames[channel]),
                cipher_pairs[channel],
                kChannelBits[channel], colors[channel]));
        }
        xormap_image::write_plot_grid_pdf(
            options.paths.results / (tag + "_correlation.pdf"), summary.name,
            "horizontal adjacent-pixel pairs: top plain, bottom cipher",
            scatter_panels, 3U);

        const auto plain_preview = xormap_color::preview_rgb565(
            image.plain, image.entry.width, image.entry.height);
        const auto cipher_preview = xormap_color::preview_rgb565(
            encrypted.cipher, image.entry.width, image.entry.height);
        xormap_color::write_rgb_comparison_pdf(
            options.paths.results / (tag + "_images.pdf"), summary.name,
            "K=512; packed 16-bit RGB565; native C++ vector report",
            plain_preview, cipher_preview, "Plain (RGB565 preview)",
            "Cipher (RGB565 preview)");
        summaries.push_back(summary);
        progress << "completed " << summary.name << '\n';
    }
    write_run_results(options, summaries);
    progress << "wrote " << summaries.size() * 3U
             << " RGB565 PDFs and results.md\n";
}

void sweep_legacy(const Options& options, std::ostream& progress)
{
    const auto images = load_converted(options);
    const auto indexes = selected_indices(images, false);
    if (options.k_values.empty()) {
        throw std::invalid_argument("legacy sweep requires K values");
    }
    xormap_image::MatlabTwister rng(2026U);
    std::vector<SweepRow> rows;
    rows.reserve(indexes.size() * options.k_values.size());
    for (const std::size_t image_index : indexes) {
        const auto& image = images[image_index];
        xormap_color::Words changed = image.plain;
        changed[(image.entry.height / 2U) * image.entry.width +
                image.entry.width / 2U] ^= 1U;
        for (const std::size_t k : options.k_values) {
            const auto key = xormap_image::secret_key(k);
            const auto begin = std::chrono::steady_clock::now();
            const auto cipher = xormap_color::encrypt_words_fast(
                image.plain, 16U, key).cipher;
            const auto cipher2 = xormap_color::encrypt_words_fast(
                changed, 16U, key).cipher;
            const double seconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - begin).count();
            const auto channels = xormap_color::rgb565_channels(cipher);
            double normalized_entropy = 0.0;
            double corr = 0.0;
            for (std::size_t channel = 0; channel < 3U; ++channel) {
                normalized_entropy += xormap_image::shannon_entropy(
                    channels[channel], kChannelBits[channel]) /
                    static_cast<double>(kChannelBits[channel]) / 3.0;
                corr += std::fabs(xormap_image::adjacent_correlation(
                    channels[channel], image.entry.width, image.entry.height,
                    xormap_image::AdjacentDirection::Horizontal,
                    options.correlation_samples, rng).correlation) / 3.0;
            }
            const auto diff = xormap_color::npcr_uaci_words(
                cipher, cipher2, kWordMaximum);
            rows.push_back({image_index, k, image.entry.height,
                image.entry.width, image.plain.size(),
                (image.plain.size() * 16U + k - 1U) / k,
                static_cast<double>(k) / 16.0, normalized_entropy, corr,
                diff.npcr_percent, diff.uaci_percent, seconds});
        }
    }
    std::filesystem::create_directories(options.paths.results);
    const auto csv_path = options.paths.results / "sweep_k_rgb565.csv";
    std::ofstream csv(csv_path);
    csv << "image,K,mean_norm_entropy_cipher,mean_abs_corrH_cipher,"
           "npcr_packed,uaci_packed,seconds\n" << std::fixed;
    for (const auto& row : rows) {
        csv << xormap_image::csv_escape(display_name(images[row.image].entry.name, false))
            << ',' << row.k << ',' << std::setprecision(6)
            << row.normalized_entropy << ',' << row.correlation << ','
            << row.npcr << ',' << row.uaci << ',' << std::setprecision(4)
            << row.seconds << '\n';
    }
    const auto ideal = xormap_image::npcr_uaci_ideal(16);
    std::vector<xormap_image::Panel> panels;
    panels.push_back(aggregate_panel("Normalized entropy", "Mean of channel maxima",
        rows, options.k_values, &SweepRow::normalized_entropy, 1.0));
    panels.push_back(aggregate_panel("Horizontal correlation", "Mean absolute correlation",
        rows, options.k_values, &SweepRow::correlation, 0.0));
    panels.push_back(aggregate_panel("NPCR", "NPCR (%)", rows,
        options.k_values, &SweepRow::npcr, ideal.npcr_percent));
    panels.push_back(aggregate_panel("UACI", "UACI (%)", rows,
        options.k_values, &SweepRow::uaci, ideal.uaci_percent));
    panels.push_back(aggregate_panel("Two-encryption time", "Seconds", rows,
        options.k_values, &SweepRow::seconds,
        std::numeric_limits<double>::quiet_NaN()));
    xormap_image::write_plot_grid_pdf(
        options.paths.results / "sweep_k_rgb565.pdf",
        "xormap RGB565: original three-image MATLAB sweep",
        "K grid and serial MATLAB Twister sampling preserved", panels, 2U);
    progress << "wrote " << csv_path << " and sweep_k_rgb565.pdf\n";
}

void sweep_all(const Options& options, std::ostream& progress)
{
    const auto images = load_converted(options);
    if (options.k_values.empty()) {
        throw std::invalid_argument("full sweep requires K values");
    }
    const std::size_t task_count = images.size() * options.k_values.size();
    std::vector<SweepRow> rows(task_count);
    progress << images.size() << " converted RGB565 images x "
             << options.k_values.size() << " K values = " << task_count
             << " native C++ tasks\n";
    xormap_image::parallel_for(task_count, options.workers,
        [&](std::size_t index) {
            const std::size_t k_index = index / images.size();
            const std::size_t image_index = index % images.size();
            const std::size_t k = options.k_values[k_index];
            const auto& image = images[image_index];
            xormap_color::Words changed = image.plain;
            changed[(image.entry.height / 2U) * image.entry.width +
                    image.entry.width / 2U] ^= 1U;
            const auto key = xormap_image::worker_secret_key(k);
            const auto begin = std::chrono::steady_clock::now();
            const auto cipher = xormap_color::encrypt_words_fast(
                image.plain, 16U, key).cipher;
            const auto cipher2 = xormap_color::encrypt_words_fast(
                changed, 16U, key).cipher;
            const double seconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - begin).count();
            const auto channels = xormap_color::rgb565_channels(cipher);
            double normalized_entropy = 0.0;
            xormap_image::MatlabThreefry rng(static_cast<std::uint32_t>(index + 1U));
            double corr = 0.0;
            for (std::size_t channel = 0; channel < 3U; ++channel) {
                normalized_entropy += xormap_image::shannon_entropy(
                    channels[channel], kChannelBits[channel]) /
                    static_cast<double>(kChannelBits[channel]) / 3.0;
                corr += std::fabs(xormap_image::adjacent_correlation(
                    channels[channel], image.entry.width, image.entry.height,
                    xormap_image::AdjacentDirection::Horizontal,
                    options.correlation_samples, rng).correlation) / 3.0;
            }
            const auto diff = xormap_color::npcr_uaci_words(
                cipher, cipher2, kWordMaximum);
            rows[index] = {image_index, k, image.entry.height, image.entry.width,
                image.plain.size(), (image.plain.size() * 16U + k - 1U) / k,
                static_cast<double>(k) / 16.0, normalized_entropy, corr,
                diff.npcr_percent, diff.uaci_percent, seconds};
        });

    std::filesystem::create_directories(options.paths.results);
    const auto csv_path = options.paths.results / "sweep_k_rgb565_all.csv";
    std::ofstream csv(csv_path);
    csv << "image,volume,K,height,width,num_pixels,iterations_per_encrypt,"
           "pixels_per_iteration,mean_norm_entropy_cipher,mean_abs_corrH_cipher,"
           "npcr_packed,uaci_packed,seconds_two_encryptions\n" << std::fixed;
    for (const auto& row : rows) {
        const auto& entry = images[row.image].entry;
        csv << xormap_image::csv_escape(entry.name) << ','
            << xormap_image::csv_escape(entry.volume) << ',' << row.k << ','
            << row.height << ',' << row.width << ',' << row.pixels << ','
            << row.iterations << ',' << std::setprecision(4)
            << row.pixels_per_iteration << ',' << std::setprecision(6)
            << row.normalized_entropy << ',' << row.correlation << ','
            << row.npcr << ',' << row.uaci << ',' << std::setprecision(4)
            << row.seconds << '\n';
    }
    const auto ideal = xormap_image::npcr_uaci_ideal(16);
    std::vector<xormap_image::Panel> panels;
    panels.push_back(aggregate_panel("Normalized entropy", "Mean of channel maxima",
        rows, options.k_values, &SweepRow::normalized_entropy, 1.0));
    panels.push_back(aggregate_panel("Horizontal correlation", "Mean absolute correlation",
        rows, options.k_values, &SweepRow::correlation, 0.0));
    panels.push_back(aggregate_panel("NPCR", "NPCR (%)", rows,
        options.k_values, &SweepRow::npcr, ideal.npcr_percent));
    panels.push_back(aggregate_panel("UACI", "UACI (%)", rows,
        options.k_values, &SweepRow::uaci, ideal.uaci_percent));
    panels.push_back(aggregate_panel("Two-encryption time", "Seconds", rows,
        options.k_values, &SweepRow::seconds,
        std::numeric_limits<double>::quiet_NaN()));
    xormap_image::write_plot_grid_pdf(
        options.paths.results / "sweep_k_rgb565_all.pdf",
        "xormap RGB565: every SIPI colour image",
        "51 RGB888 sources converted to packed RGB565; native C++ parallel sweep",
        panels, 2U);
    progress << "wrote " << csv_path << " and sweep_k_rgb565_all.pdf\n";
}

}  // namespace xormap_rgb565
