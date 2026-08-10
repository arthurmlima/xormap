#include "xormap_color/rgb888.hpp"

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
#include <numeric>
#include <ostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace xormap_color::rgb888 {
namespace {

constexpr unsigned int kWordBits = 24U;
constexpr Word kWordMaximum = 0xFFFFFFU;
constexpr std::size_t kFlipBit = 1U;

struct LoadedImage {
    ColorManifestEntry entry;
    RgbImage rgb;
    Words plain;
};

struct SweepRow {
    std::size_t image = 0;
    std::size_t k = 0;
    std::size_t height = 0;
    std::size_t width = 0;
    std::size_t pixels = 0;
    double entropy = 0.0;
    double correlation = 0.0;
    double npcr = 0.0;
    double uaci = 0.0;
};

struct AnalysisRow {
    std::size_t image = 0;
    std::size_t k = 0;
    std::size_t pixels = 0;
    double npcr_key = 0.0;
    double uaci_key = 0.0;
    double psnr_pair = 0.0;
    double psnr_wrong = 0.0;
    double chi_plain = 0.0;
    double chi_cipher = 0.0;
    double chi_critical = 0.0;
    double psnr_cipher = 0.0;
    double psnr_roundtrip = 0.0;
    std::uint32_t checksum = 0;
    std::size_t first_word = 0;
};

struct BitRow {
    std::size_t k = 0;
    std::size_t bit = 0;
    double npcr = 0.0;
    double uaci = 0.0;
    double psnr = 0.0;
};

[[nodiscard]] Words xor_words_local(const Words& first, const Words& second)
{
    if (first.size() != second.size()) {
        throw std::invalid_argument("word XOR operands must have equal sizes");
    }
    Words result(first.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        result[i] = first[i] ^ second[i];
    }
    return result;
}

[[nodiscard]] std::vector<LoadedImage> load_images(const Options& options)
{
    const auto manifest = read_color_manifest(options.paths.manifest,
                                              options.paths.images);
    std::vector<LoadedImage> images;
    images.reserve(manifest.size());
    for (const auto& entry : manifest) {
        RgbImage rgb = load_tiff_rgb8(entry.path);
        Words plain = pack_rgb888(rgb);
        images.push_back({entry, std::move(rgb), std::move(plain)});
    }
    return images;
}

[[nodiscard]] xormap_image::Series mean_series(
    const std::string& label, const std::vector<std::size_t>& row_k,
    const std::vector<double>& values, const std::vector<std::size_t>& ks,
    xormap_image::Color color)
{
    xormap_image::Series series;
    series.label = label;
    series.style = xormap_image::SeriesStyle::Line;
    series.line_width = 2.0;
    series.marker_radius = 2.7;
    series.color = color;
    for (const std::size_t k : ks) {
        long double sum = 0.0L;
        std::size_t count = 0;
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (row_k[i] == k && std::isfinite(values[i])) {
                sum += values[i];
                ++count;
            }
        }
        if (count != 0U) {
            series.points.push_back({static_cast<double>(k),
                                     static_cast<double>(
                                         sum / static_cast<long double>(count))});
        }
    }
    return series;
}

[[nodiscard]] xormap_image::Panel metric_panel(
    const std::string& title, const std::string& y_label,
    const std::vector<std::size_t>& row_k, const std::vector<double>& values,
    const std::vector<std::size_t>& ks, double ideal,
    xormap_image::Color mean_color = {0.922, 0.408, 0.204, 1.0})
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
    points.points.reserve(values.size());
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (std::isfinite(values[i])) {
            points.points.push_back(
                {static_cast<double>(row_k[i]), values[i]});
        }
    }
    panel.series.push_back(std::move(points));
    panel.series.push_back(mean_series("mean", row_k, values, ks, mean_color));
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

// Plots NPCR and UACI as two mean-over-images lines in one panel instead of
// two separate panels, since both are percentages read against K.
[[nodiscard]] xormap_image::Panel combined_npcr_uaci_panel(
    const std::string& title, const std::vector<std::size_t>& row_k,
    const std::vector<double>& npcr_values, double npcr_ideal,
    const std::vector<double>& uaci_values, double uaci_ideal,
    const std::vector<std::size_t>& ks)
{
    constexpr xormap_image::Color kNpcrColor{0.922, 0.408, 0.204, 1.0};
    constexpr xormap_image::Color kUaciColor{0.165, 0.471, 0.839, 1.0};
    xormap_image::Panel panel;
    panel.title = title;
    panel.x_label = "K (state bits)";
    panel.y_label = "Percent (%)";
    panel.series.push_back(
        mean_series("NPCR", row_k, npcr_values, ks, kNpcrColor));
    panel.series.push_back(
        mean_series("UACI", row_k, uaci_values, ks, kUaciColor));
    if (std::isfinite(npcr_ideal)) {
        panel.reference_lines.push_back({npcr_ideal, "NPCR ideal",
            {kNpcrColor.red, kNpcrColor.green, kNpcrColor.blue, 0.5},
            xormap_image::ReferenceOrientation::Horizontal, 1.0});
    }
    if (std::isfinite(uaci_ideal)) {
        panel.reference_lines.push_back({uaci_ideal, "UACI ideal",
            {kUaciColor.red, kUaciColor.green, kUaciColor.blue, 0.5},
            xormap_image::ReferenceOrientation::Horizontal, 1.0});
    }
    panel.x_range = xormap_image::AxisRange{
        static_cast<double>(ks.front()) - 18.0,
        static_cast<double>(ks.back()) + 18.0};
    return panel;
}

[[nodiscard]] xormap_image::Panel histogram_distribution_panel(
    const std::vector<AnalysisRow>& rows)
{
    std::vector<double> plain;
    std::vector<double> cipher;
    plain.reserve(rows.size());
    cipher.reserve(rows.size());
    for (const auto& row : rows) {
        plain.push_back(std::log10(std::max(row.chi_plain, 1.0)));
        cipher.push_back(std::log10(std::max(row.chi_cipher, 1.0)));
    }
    const double minimum = std::min(*std::min_element(plain.begin(), plain.end()),
                                    *std::min_element(cipher.begin(), cipher.end()));
    const double maximum = std::max(*std::max_element(plain.begin(), plain.end()),
                                    *std::max_element(cipher.begin(), cipher.end()));
    constexpr std::size_t bins = 40U;
    const double span = std::max(maximum - minimum, 1.0);
    const double width = span / static_cast<double>(bins);
    auto make_bars = [&](const std::vector<double>& values,
                         std::string label, xormap_image::Color color) {
        std::vector<std::size_t> counts(bins, 0U);
        for (const double value : values) {
            const std::size_t bin = std::min<std::size_t>(
                bins - 1U, static_cast<std::size_t>((value - minimum) / width));
            ++counts[bin];
        }
        xormap_image::Series series;
        series.label = std::move(label);
        series.style = xormap_image::SeriesStyle::Bars;
        series.color = color;
        for (std::size_t bin = 0; bin < bins; ++bin) {
            series.points.push_back(
                {minimum + (static_cast<double>(bin) + 0.5) * width,
                 static_cast<double>(counts[bin])});
        }
        return series;
    };
    xormap_image::Panel panel;
    panel.title = "Plain vs cipher chi-square";
    panel.x_label = "log10 chi-square";
    panel.y_label = "Count";
    panel.series.push_back(make_bars(plain, "plain", {0.922, 0.408, 0.204, 0.7}));
    panel.series.push_back(make_bars(cipher, "cipher", {0.165, 0.471, 0.839, 0.7}));
    panel.reference_lines.push_back({std::log10(rows.front().chi_critical),
        "5% critical", {0.35, 0.35, 0.35, 0.8},
        xormap_image::ReferenceOrientation::Vertical, 1.0});
    return panel;
}

[[nodiscard]] xormap_image::Panel rgb_histogram_panel(
    const std::string& title,
    const std::array<std::vector<std::uint8_t>, 3>& channels)
{
    xormap_image::Panel panel;
    panel.title = title;
    panel.x_label = "level";
    panel.y_label = "count";
    const std::array<std::string, 3> labels{{"R", "G", "B"}};
    const std::array<xormap_image::Color, 3> colors{{
        {0.85, 0.20, 0.20, 0.88}, {0.20, 0.60, 0.25, 0.88},
        {0.20, 0.35, 0.85, 0.88}}};
    for (std::size_t channel = 0; channel < 3U; ++channel) {
        std::array<std::size_t, 256> counts{};
        for (const std::uint8_t value : channels[channel]) {
            ++counts[value];
        }
        xormap_image::Series series;
        series.label = labels[channel];
        series.style = xormap_image::SeriesStyle::Line;
        series.line_width = 1.2;
        series.color = colors[channel];
        for (std::size_t level = 0; level < counts.size(); ++level) {
            series.points.push_back(
                {static_cast<double>(level), static_cast<double>(counts[level])});
        }
        panel.series.push_back(std::move(series));
    }
    panel.reference_lines.push_back({
        static_cast<double>(channels[0].size()) / 256.0, "flat",
        {0.35, 0.35, 0.35, 0.8},
        xormap_image::ReferenceOrientation::Horizontal, 1.0});
    panel.x_range = xormap_image::AxisRange{-2.0, 257.0};
    return panel;
}

[[nodiscard]] std::vector<std::size_t> bit_positions(std::size_t k)
{
    const std::size_t count = std::min<std::size_t>(k, 24U);
    std::vector<std::size_t> positions;
    positions.reserve(count);
    if (count == 1U) {
        return {1U};
    }
    for (std::size_t i = 0; i < count; ++i) {
        const double value = 1.0 + static_cast<double>(i) *
            static_cast<double>(k - 1U) / static_cast<double>(count - 1U);
        const std::size_t rounded = static_cast<std::size_t>(std::floor(value + 0.5));
        if (positions.empty() || positions.back() != rounded) {
            positions.push_back(rounded);
        }
    }
    return positions;
}

[[nodiscard]] std::vector<BitRow> run_bit_study(
    const LoadedImage& image, const std::vector<std::size_t>& ks)
{
    std::vector<BitRow> rows;
    for (const std::size_t k : ks) {
        const xormap_image::Bits key1 = xormap_image::secret_key(k);
        const Words cipher1 = encrypt_words_fast(image.plain, kWordBits, key1).cipher;
        const auto rgb1 = unpack_rgb888(cipher1, image.rgb.width, image.rgb.height);
        for (const std::size_t bit : bit_positions(k)) {
            xormap_image::Bits key2 = key1;
            key2[bit - 1U] ^= 1U;
            const Words cipher2 = encrypt_words_fast(image.plain, kWordBits, key2).cipher;
            const auto metrics = npcr_uaci_words(cipher1, cipher2, kWordMaximum);
            const auto rgb2 = unpack_rgb888(cipher2, image.rgb.width, image.rgb.height);
            rows.push_back({k, bit, metrics.npcr_percent, metrics.uaci_percent,
                            xormap_image::psnr_db(rgb1.pixels, rgb2.pixels).psnr_db});
        }
    }
    return rows;
}

// NPCR/UACI appear in both the sweep pass (plaintext-bit-flip diffusion) and
// the key-sensitivity pass (key-bit-flip diffusion) below. They measure two
// different perturbations, so the panel titles say which is which rather
// than letting two identically-labelled charts show different numbers.
[[nodiscard]] std::vector<xormap_image::Panel> build_sweep_panels(
    const std::vector<SweepRow>& rows, const std::vector<std::size_t>& ks)
{
    std::vector<std::size_t> row_k;
    std::vector<double> entropy, corr, npcr, uaci;
    for (const auto& row : rows) {
        row_k.push_back(row.k);
        entropy.push_back(row.entropy);
        corr.push_back(row.correlation);
        npcr.push_back(row.npcr);
        uaci.push_back(row.uaci);
    }
    const auto ideal = xormap_image::npcr_uaci_ideal(24);
    std::vector<xormap_image::Panel> panels;
    panels.push_back(metric_panel("Cipher entropy", "Mean entropy (bits)",
                                  row_k, entropy, ks, 8.0));
    panels.push_back(metric_panel("Horizontal correlation", "Mean absolute correlation",
                                  row_k, corr, ks, 0.0));
    panels.push_back(combined_npcr_uaci_panel(
        "NPCR/UACI (plaintext bit flip)", row_k, npcr, ideal.npcr_percent,
        uaci, ideal.uaci_percent, ks));
    return panels;
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
    for (const std::size_t k : k_values(24U, 24U, 384U)) {
        const auto plan = xormap_image::make_transform_plan(k);
        for (std::size_t bit = 0; bit < k; ++bit) {
            xormap_image::Bits basis(k, 0U);
            basis[bit] = 1U;
            if (xormap_image::transform_canonical(basis) !=
                xormap_image::transform_fast(basis, plan)) {
                throw std::runtime_error("canonical/fast basis mismatch at K=" +
                                         std::to_string(k));
            }
        }
        xormap_image::Bits seed(k, 0U);
        for (std::size_t i = 0; i < k; ++i) {
            const std::uint64_t value = i * 78U + (i / 3U) * 19U + 60U;
            seed[i] = static_cast<std::uint8_t>((value & 1U) ^ ((value >> 3U) & 1U));
        }
        if (keystream_words_fast(seed, 24U, 100U) !=
            keystream_words_canonical(seed, 24U, 100U)) {
            throw std::runtime_error("fast/canonical word stream mismatch at K=" +
                                     std::to_string(k));
        }
        const RgbImage image(2U, 2U,
            {0U, 1U, 255U, 127U, 64U, 200U,
             17U, 42U, 99U, 128U, 13U, 240U});
        const Words plain = pack_rgb888(image);
        xormap_image::Bits key(k, 0U);
        for (std::size_t i = 0; i < k; ++i) {
            const std::uint64_t value = i * 96U + (i / 3U) * 19U + 258U;
            key[i] = static_cast<std::uint8_t>((value & 1U) ^ ((value >> 3U) & 1U));
        }
        const auto encrypted = encrypt_words_fast(plain, 24U, key);
        if (decrypt_words_fast(encrypted.cipher, 24U, encrypted.seed) != plain ||
            encrypted.iterations != (plain.size() * 24U + k - 1U) / k) {
            throw std::runtime_error("RGB888 round-trip mismatch at K=" +
                                     std::to_string(k));
        }
        progress << "verified RGB888 K=" << k << '\n';
    }
}

void run_tests(const Options& options, std::ostream& progress)
{
    if (options.k_values.empty() || options.correlation_samples == 0U) {
        throw std::invalid_argument("run-tests requires K values and correlation samples");
    }
    const auto images = load_images(options);
    const std::size_t task_count = images.size() * options.k_values.size();
    progress << images.size() << " images x " << options.k_values.size()
             << " K values = " << task_count << " native C++ tasks per pass\n";

    std::vector<SweepRow> rows(task_count);
    xormap_image::parallel_for(task_count, options.workers, [&](std::size_t index) {
        const std::size_t k_index = index / images.size();
        const std::size_t image_index = index % images.size();
        const std::size_t k = options.k_values[k_index];
        const LoadedImage& image = images[image_index];
        Words changed_plain = image.plain;
        changed_plain[(image.rgb.height / 2U) * image.rgb.width +
                      image.rgb.width / 2U] ^= 1U;
        const xormap_image::Bits key = xormap_image::worker_secret_key(k);
        const Words cipher = encrypt_words_fast(image.plain, kWordBits, key).cipher;
        const Words cipher2 = encrypt_words_fast(changed_plain, kWordBits, key).cipher;
        const auto channels = rgb888_channels(cipher);
        const double entropy = (xormap_image::shannon_entropy(channels[0]) +
                                xormap_image::shannon_entropy(channels[1]) +
                                xormap_image::shannon_entropy(channels[2])) / 3.0;
        xormap_image::MatlabThreefry rng(static_cast<std::uint32_t>(index + 1U));
        double corr = 0.0;
        for (const auto& channel : channels) {
            corr += std::fabs(xormap_image::adjacent_correlation(
                channel, image.rgb.width, image.rgb.height,
                xormap_image::AdjacentDirection::Horizontal,
                options.correlation_samples, rng).correlation) / 3.0;
        }
        const auto differential = npcr_uaci_words(cipher, cipher2, kWordMaximum);
        rows[index] = {image_index, k, image.rgb.height, image.rgb.width,
                       image.plain.size(), entropy, corr,
                       differential.npcr_percent, differential.uaci_percent};
    });

    std::filesystem::create_directories(options.paths.results);

    std::vector<AnalysisRow> analysis_rows(task_count);
    xormap_image::parallel_for(task_count, options.workers, [&](std::size_t index) {
        const std::size_t k_index = index / images.size();
        const std::size_t image_index = index % images.size();
        const std::size_t k = options.k_values[k_index];
        const LoadedImage& image = images[image_index];
        const xormap_image::Bits key1 = xormap_image::worker_secret_key(k);
        xormap_image::Bits key2 = key1;
        key2[kFlipBit - 1U] ^= 1U;
        const auto encrypted1 = encrypt_words_fast(image.plain, kWordBits, key1);
        const Words cipher2 = encrypt_words_fast(image.plain, kWordBits, key2).cipher;
        const Words difference = xor_words_local(encrypted1.cipher, cipher2);
        const Words wrong = xor_words_local(image.plain, difference);
        const Words recovered = decrypt_words_fast(
            encrypted1.cipher, kWordBits, encrypted1.seed);
        if (recovered != image.plain) {
            throw std::runtime_error("RGB888 analysis round-trip failure");
        }
        if (index == 0U) {
            const auto second_seed = encrypt_words_fast(image.plain, kWordBits, key2).seed;
            if (decrypt_words_fast(encrypted1.cipher, kWordBits, second_seed) != wrong) {
                throw std::runtime_error("wrong-key XOR shortcut mismatch");
            }
        }

        const auto packed_metrics = npcr_uaci_words(
            encrypted1.cipher, cipher2, kWordMaximum);
        const RgbImage plain_rgb = image.rgb;
        const RgbImage cipher_rgb = unpack_rgb888(
            encrypted1.cipher, image.rgb.width, image.rgb.height);
        const RgbImage cipher2_rgb = unpack_rgb888(
            cipher2, image.rgb.width, image.rgb.height);
        const RgbImage wrong_rgb = unpack_rgb888(
            wrong, image.rgb.width, image.rgb.height);
        const auto plain_channels = rgb888_channels(image.plain);
        const auto cipher_channels = rgb888_channels(encrypted1.cipher);
        double chi_plain = 0.0;
        double chi_cipher = 0.0;
        double critical = 0.0;
        for (std::size_t channel = 0; channel < 3U; ++channel) {
            const auto plain_chi = xormap_image::chi_square_uniformity(
                plain_channels[channel]);
            const auto cipher_chi = xormap_image::chi_square_uniformity(
                cipher_channels[channel]);
            chi_plain += plain_chi.statistic / 3.0;
            chi_cipher += cipher_chi.statistic / 3.0;
            critical = plain_chi.critical_value;
        }
        analysis_rows[index] = {
            image_index, k, image.plain.size(), packed_metrics.npcr_percent,
            packed_metrics.uaci_percent,
            xormap_image::psnr_db(cipher_rgb.pixels, cipher2_rgb.pixels).psnr_db,
            xormap_image::psnr_db(plain_rgb.pixels, wrong_rgb.pixels).psnr_db,
            chi_plain, chi_cipher, critical,
            xormap_image::psnr_db(plain_rgb.pixels, cipher_rgb.pixels).psnr_db,
            psnr_words(image.plain, recovered, 255.0).psnr_db,
            diff_checksum_words(difference), first_differing_word(difference)};
    });

    for (const std::size_t k : options.k_values) {
        std::uint32_t expected = 0U;
        bool initialized = false;
        for (const auto& row : analysis_rows) {
            if (row.k != k) {
                continue;
            }
            if (!initialized) {
                expected = row.checksum;
                initialized = true;
            } else if (row.checksum != expected) {
                throw std::runtime_error(
                    "key-difference checksum is not image-independent at K=" +
                    std::to_string(k));
            }
        }
    }

    const std::vector<BitRow> bits = run_bit_study(images.front(), options.k_values);

    // ---- one combined CSV: main (image,K) table, then the per-bit study ----
    // rows[i] and analysis_rows[i] refer to the same (image,K) task, since
    // both passes share images/options.k_values and the same index formula.
    const auto csv_path = options.paths.results / "sweep_k_rgb888.csv";
    std::ofstream csv(csv_path);
    if (!csv) {
        throw std::runtime_error("could not create " + csv_path.string());
    }
    csv << "image,volume,K,height,width,num_pixels,mean_entropy_cipher,"
           "mean_abs_corrH_cipher,npcr_plaintext_flip_packed,"
           "uaci_plaintext_flip_packed,flipped_key_bit,"
           "npcr_key_flip_packed,uaci_key_flip_packed,psnr_cipher_pair_db,"
           "psnr_plain_wrongkey_db,first_differing_word,key_diff_checksum,"
           "mean_chi2_plain,mean_chi2_cipher,chi2_critical_005,"
           "cipher_uniform_pass,psnr_plain_cipher_db,psnr_roundtrip_db\n";
    csv << std::fixed;
    for (std::size_t i = 0; i < task_count; ++i) {
        const SweepRow& sweep_row = rows[i];
        const AnalysisRow& analysis_row = analysis_rows[i];
        const auto& entry = images[sweep_row.image].entry;
        csv << xormap_image::csv_escape(entry.name) << ','
            << xormap_image::csv_escape(entry.volume) << ',' << sweep_row.k
            << ',' << sweep_row.height << ',' << sweep_row.width << ','
            << sweep_row.pixels << ',' << std::setprecision(6)
            << sweep_row.entropy << ',' << sweep_row.correlation << ','
            << sweep_row.npcr << ',' << sweep_row.uaci << ',' << kFlipBit
            << ',' << std::setprecision(6) << analysis_row.npcr_key << ','
            << analysis_row.uaci_key << ',' << std::setprecision(4)
            << analysis_row.psnr_pair << ',' << analysis_row.psnr_wrong << ','
            << analysis_row.first_word << ',' << analysis_row.checksum << ','
            << std::setprecision(4) << analysis_row.chi_plain << ','
            << analysis_row.chi_cipher << ',' << analysis_row.chi_critical
            << ',' << (analysis_row.chi_cipher <= analysis_row.chi_critical ? 1 : 0)
            << ',' << analysis_row.psnr_cipher << ','
            << (std::isinf(analysis_row.psnr_roundtrip)
                    ? "Inf"
                    : std::to_string(analysis_row.psnr_roundtrip))
            << '\n';
    }
    csv << '\n';
    csv << "K,flipped_key_bit,npcr_key_bitstudy_packed,"
           "uaci_key_bitstudy_packed,psnr_cipher_pair_bitstudy_db\n";
    for (const auto& row : bits) {
        csv << row.k << ',' << row.bit << ',' << std::setprecision(6)
            << row.npcr << ',' << row.uaci << ',' << std::setprecision(4)
            << row.psnr << '\n';
    }
    csv.close();

    std::vector<std::size_t> row_k;
    std::vector<double> npcr, uaci, wrong_psnr, cipher_psnr, pair_bit_npcr;
    for (const auto& row : analysis_rows) {
        row_k.push_back(row.k);
        npcr.push_back(row.npcr_key);
        uaci.push_back(row.uaci_key);
        wrong_psnr.push_back(row.psnr_wrong);
        cipher_psnr.push_back(row.psnr_cipher);
    }
    std::vector<std::size_t> bit_k;
    for (const auto& row : bits) {
        bit_k.push_back(row.k);
        pair_bit_npcr.push_back(row.npcr);
    }
    const auto ideal = xormap_image::npcr_uaci_ideal(24);

    // ---- assemble every panel from both passes into one combined report ----
    std::vector<xormap_image::Panel> panels = build_sweep_panels(rows, options.k_values);

    panels.push_back(combined_npcr_uaci_panel(
        "NPCR/UACI (key bit flip)", row_k, npcr, ideal.npcr_percent, uaci,
        ideal.uaci_percent, options.k_values));
    panels.push_back(metric_panel("Wrong-key decryption", "PSNR (dB)",
                                  row_k, wrong_psnr, options.k_values,
                                  std::numeric_limits<double>::quiet_NaN(),
                                  {0.165, 0.471, 0.839, 1.0}));
    panels.push_back(metric_panel("By flipped key-bit position", "NPCR (%)",
                                  bit_k, pair_bit_npcr, options.k_values,
                                  ideal.npcr_percent,
                                  {0.165, 0.471, 0.839, 1.0}));

    std::vector<double> chi_cipher;
    for (const auto& row : analysis_rows) {
        chi_cipher.push_back(row.chi_cipher);
    }
    const xormap_image::Bits example_key =
        xormap_image::secret_key(options.k_values.back());
    const Words example_cipher = encrypt_words_fast(
        images.front().plain, kWordBits, example_key).cipher;
    panels.push_back(metric_panel(
        "Cipher chi-square", "Mean chi-square (255 dof)", row_k, chi_cipher,
        options.k_values, analysis_rows.front().chi_critical,
        {0.165, 0.471, 0.839, 1.0}));
    panels.push_back(histogram_distribution_panel(analysis_rows));
    panels.push_back(rgb_histogram_panel(
        "Cipher histogram, R/G/B (K=" +
            std::to_string(options.k_values.back()) + ")",
        rgb888_channels(example_cipher)));
    const std::size_t pass_count = static_cast<std::size_t>(std::count_if(
        analysis_rows.begin(), analysis_rows.end(), [](const AnalysisRow& row) {
            return row.chi_cipher <= row.chi_critical;
        }));

    panels.push_back(metric_panel("Plain vs cipher", "PSNR (dB)", row_k,
                                  cipher_psnr, options.k_values,
                                  std::numeric_limits<double>::quiet_NaN(),
                                  {0.165, 0.471, 0.839, 1.0}));
    panels.push_back(metric_panel("Plain vs wrong-key decryption",
                                  "PSNR (dB)", row_k, wrong_psnr,
                                  options.k_values,
                                  std::numeric_limits<double>::quiet_NaN(),
                                  {0.165, 0.471, 0.839, 1.0}));

    xormap_image::write_plot_grid_pdf(
        options.paths.results / "sweep_k_rgb888.pdf",
        "xormap RGB888: every SIPI colour image — full test report",
        std::to_string(images.size()) + " images; sweep (entropy/correlation/"
            "plaintext-bit diffusion) + key-bit sensitivity + histogram "
            "(" + std::to_string(pass_count) + "/" + std::to_string(analysis_rows.size()) +
            " cipher cases pass chi-square at alpha=0.05) + PSNR, native C++ parallel",
        panels, 3U);
    progress << "wrote one combined " << csv_path << " (" << task_count
              << " image/K rows + " << bits.size() << " bit-study rows) and "
                 "combined all " << panels.size()
              << " panels into one sweep_k_rgb888.pdf\n";
}

}  // namespace xormap_color::rgb888
