% run_all_rgb565.m
% RGB565 analogue of xormap_image_matlab/run_all.m: converts each
% downloaded color image to packed 16-bit RGB565, encrypts it with the
% xormap-based stream cipher (xormap_rgb565_encrypt.m), and runs the same
% evaluation battery per channel (R:5 bits, G:6 bits, B:5 bits) plus
% NPCR/UACI on the packed word. Writes figures and a results table to
% results/.
%
% Run download_images.m first if images/*.tiff aren't there yet.

clear all;
clc

this_dir = fileparts(mfilename('fullpath'));
addpath(this_dir);
addpath(fullfile(this_dir, '..', 'xormap_matlab'));
addpath(fullfile(this_dir, '..', 'xormap_image_matlab'));  % sha256_bits, hash_expand_bits, secret_key, shannon_entropy, adjacent_correlation, npcr_uaci

results_dir = fullfile(this_dir, 'results');
if ~exist(results_dir, 'dir')
    mkdir(results_dir);
end

images = struct( ...
    'file', {'4.2.03.tiff', '4.2.05.tiff', '4.2.07.tiff'}, ...
    'name', {'4.2.03 (Mandrill, 512x512)', '4.2.05 (Airplane F-16, 512x512)', '4.2.07 (Peppers, 512x512)'});

K = 512;  % 512/16 = 32 pixels of keystream per xormap_transform iteration
key_bits = secret_key(K);
NUM_CORR_SAMPLES = 5000;
NUM_SCATTER_SAMPLES = 3000;
CHANNELS = {'R', 'G', 'B'};
NBITS = [5, 6, 5];

rng(2026);

summary_entropy = {};
summary_corr = {};
summary_npcr_uaci = {};

for ii = 1:numel(images)
    fname = fullfile(this_dir, 'images', images(ii).file);
    if ~exist(fname, 'file')
        error('Image not found: %s. Run download_images.m first.', fname);
    end

    rgb = imread(fname);
    if ndims(rgb) ~= 3 || size(rgb, 3) ~= 3 || ~isa(rgb, 'uint8')
        error('%s is not an 8-bit truecolor image', fname);
    end
    plain = rgb888_to_rgb565(rgb);

    fprintf('\n=== %s ===\n', images(ii).name);

    % --- encrypt + round-trip correctness check ---
    [cipher, seed] = xormap_rgb565_encrypt(plain, key_bits);
    recovered = xormap_rgb565_decrypt(cipher, seed);
    assert(isequal(recovered, plain), 'round-trip failed for %s', images(ii).name);
    fprintf('Round-trip OK (decrypt(encrypt(P)) == P)\n');

    % --- NPCR/UACI: flip the LSB of the centre pixel's packed word ---
    % Ideal NPCR/UACI depend on the element's level count L=2^nbits (see
    % npcr_uaci_ideal.m); the commonly-quoted 99.6094%/33.4635% is the
    % L=256 (8-bit) case, not applicable as-is to a 16-bit packed word.
    plain2 = plain;
    r0 = floor(size(plain, 1) / 2) + 1;
    c0 = floor(size(plain, 2) / 2) + 1;
    plain2(r0, c0) = bitxor(plain2(r0, c0), uint16(1));
    cipher2 = xormap_rgb565_encrypt(plain2, key_bits);
    [npcr, uaci] = npcr_uaci(cipher, cipher2, 65535);
    [npcr_ideal16, uaci_ideal16] = npcr_uaci_ideal(16);
    fprintf('NPCR (packed word) = %.4f%%  (ideal %.4f%%)   UACI = %.4f%%  (ideal %.4f%%)\n', ...
        npcr, npcr_ideal16, uaci, uaci_ideal16);

    % --- per-channel entropy / correlation / NPCR / UACI ---
    [Rp, Gp, Bp] = rgb565_to_channels(plain);
    [Rc, Gc, Bc] = rgb565_to_channels(cipher);
    [R2c, G2c, B2c] = rgb565_to_channels(cipher2);
    plain_ch = {Rp, Gp, Bp};
    cipher_ch = {Rc, Gc, Bc};
    cipher2_ch = {R2c, G2c, B2c};

    e_plain = zeros(1, 3); e_cipher = zeros(1, 3);
    corrH_p = zeros(1, 3); corrV_p = zeros(1, 3); corrD_p = zeros(1, 3);
    corrH_c = zeros(1, 3); corrV_c = zeros(1, 3); corrD_c = zeros(1, 3);
    npcr_ch = zeros(1, 3); uaci_ch = zeros(1, 3);

    for c = 1:3
        e_plain(c) = shannon_entropy(plain_ch{c}, NBITS(c));
        e_cipher(c) = shannon_entropy(cipher_ch{c}, NBITS(c));
        corrH_p(c) = adjacent_correlation(plain_ch{c}, 'horizontal', NUM_CORR_SAMPLES);
        corrV_p(c) = adjacent_correlation(plain_ch{c}, 'vertical', NUM_CORR_SAMPLES);
        corrD_p(c) = adjacent_correlation(plain_ch{c}, 'diagonal', NUM_CORR_SAMPLES);
        corrH_c(c) = adjacent_correlation(cipher_ch{c}, 'horizontal', NUM_CORR_SAMPLES);
        corrV_c(c) = adjacent_correlation(cipher_ch{c}, 'vertical', NUM_CORR_SAMPLES);
        corrD_c(c) = adjacent_correlation(cipher_ch{c}, 'diagonal', NUM_CORR_SAMPLES);
        [npcr_ch(c), uaci_ch(c)] = npcr_uaci(cipher_ch{c}, cipher2_ch{c}, 2^NBITS(c) - 1);

        [npcr_ideal_c, uaci_ideal_c] = npcr_uaci_ideal(NBITS(c));
        fprintf('%s  entropy plain=%.4f/%.0f  cipher=%.4f/%.0f   corrH %+.4f -> %+.4f   corrV %+.4f -> %+.4f   corrD %+.4f -> %+.4f   NPCR=%.4f (ideal %.4f)  UACI=%.4f (ideal %.4f)\n', ...
            CHANNELS{c}, e_plain(c), NBITS(c), e_cipher(c), NBITS(c), ...
            corrH_p(c), corrH_c(c), corrV_p(c), corrV_c(c), corrD_p(c), corrD_c(c), ...
            npcr_ch(c), npcr_ideal_c, uaci_ch(c), uaci_ideal_c);
    end

    tag = erase(images(ii).file, '.tiff');

    % --- plain vs cipher color preview ---
    fh = figure('Visible', 'off', 'Position', [100 100 900 460]);
    subplot(1, 2, 1); image(rgb565_to_rgb888_preview(plain)); axis image off; title('Plain (RGB565 preview)');
    subplot(1, 2, 2); image(rgb565_to_rgb888_preview(cipher)); axis image off; title('Cipher (RGB565 preview)');
    sgtitle(images(ii).name, 'Interpreter', 'none');
    exportgraphics(fh, fullfile(results_dir, [tag '_images.pdf']), ...
        'ContentType', 'vector', 'BackgroundColor', 'white');
    close(fh);

    % --- per-channel histograms: rows = plain/cipher, cols = R/G/B ---
    fh = figure('Visible', 'off', 'Position', [100 100 1200 700]);
    for c = 1:3
        subplot(2, 3, c);
        plot_channel_histogram(plain_ch{c}, NBITS(c));
        title(sprintf('Plain %s', CHANNELS{c}));
        subplot(2, 3, 3 + c);
        plot_channel_histogram(cipher_ch{c}, NBITS(c));
        title(sprintf('Cipher %s', CHANNELS{c}));
    end
    sgtitle(images(ii).name, 'Interpreter', 'none');
    exportgraphics(fh, fullfile(results_dir, [tag '_histogram.pdf']), ...
        'ContentType', 'vector', 'BackgroundColor', 'white');
    close(fh);

    % --- per-channel horizontal-correlation scatter: rows = plain/cipher, cols = R/G/B ---
    fh = figure('Visible', 'off', 'Position', [100 100 1200 700]);
    for c = 1:3
        maxv = 2^NBITS(c) - 1;
        [~, xp, yp] = adjacent_correlation(plain_ch{c}, 'horizontal', NUM_SCATTER_SAMPLES);
        [~, xc, yc] = adjacent_correlation(cipher_ch{c}, 'horizontal', NUM_SCATTER_SAMPLES);
        subplot(2, 3, c);
        scatter(xp, yp, 4, 'filled'); axis([0 maxv 0 maxv]); axis square;
        xlabel('pixel(x,y)'); ylabel('pixel(x,y+1)'); title(sprintf('Plain %s', CHANNELS{c}));
        subplot(2, 3, 3 + c);
        scatter(xc, yc, 4, 'filled'); axis([0 maxv 0 maxv]); axis square;
        xlabel('pixel(x,y)'); ylabel('pixel(x,y+1)'); title(sprintf('Cipher %s', CHANNELS{c}));
    end
    sgtitle([images(ii).name ' (horizontal)'], 'Interpreter', 'none');
    exportgraphics(fh, fullfile(results_dir, [tag '_correlation.pdf']), ...
        'ContentType', 'vector', 'BackgroundColor', 'white');
    close(fh);

    summary_entropy(end + 1, :) = {images(ii).name, ...
        e_plain(1), e_cipher(1), e_plain(2), e_cipher(2), e_plain(3), e_cipher(3), npcr, uaci}; %#ok<SAGROW>
    summary_corr(end + 1, :) = {images(ii).name, ...
        corrH_p(1), corrH_c(1), corrH_p(2), corrH_c(2), corrH_p(3), corrH_c(3)}; %#ok<SAGROW>
    summary_npcr_uaci(end + 1, :) = {images(ii).name, ...
        npcr_ch(1), uaci_ch(1), npcr_ch(2), uaci_ch(2), npcr_ch(3), uaci_ch(3)}; %#ok<SAGROW>
end

% --- results table ---
md_path = fullfile(results_dir, 'results.md');
fid = fopen(md_path, 'w');
[npcr_ideal16, uaci_ideal16] = npcr_uaci_ideal(16);
fprintf(fid, '## Entropy (bits, ideal R/B=5.0, G=6.0) and NPCR/UACI on the packed 16-bit word (ideal %.4f%%/%.4f%%)\n\n', ...
    npcr_ideal16, uaci_ideal16);
fprintf(fid, '| Image | Entropy R (plain) | Entropy R (cipher) | Entropy G (plain) | Entropy G (cipher) | Entropy B (plain) | Entropy B (cipher) | NPCR %% | UACI %% |\n');
fprintf(fid, '|---|---|---|---|---|---|---|---|---|\n');
for r = 1:size(summary_entropy, 1)
    fprintf(fid, '| %s | %.4f | %.4f | %.4f | %.4f | %.4f | %.4f | %.4f | %.4f |\n', summary_entropy{r, :});
end
fprintf(fid, '\n## Horizontal adjacent-pixel correlation, per channel\n\n');
fprintf(fid, '| Image | Corr-H R (plain) | Corr-H R (cipher) | Corr-H G (plain) | Corr-H G (cipher) | Corr-H B (plain) | Corr-H B (cipher) |\n');
fprintf(fid, '|---|---|---|---|---|---|---|\n');
for r = 1:size(summary_corr, 1)
    fprintf(fid, '| %s | %+.4f | %+.4f | %+.4f | %+.4f | %+.4f | %+.4f |\n', summary_corr{r, :});
end
[npcr_ideal_r, uaci_ideal_r] = npcr_uaci_ideal(NBITS(1));
[npcr_ideal_g, uaci_ideal_g] = npcr_uaci_ideal(NBITS(2));
[npcr_ideal_b, uaci_ideal_b] = npcr_uaci_ideal(NBITS(3));
fprintf(fid, '\n## NPCR/UACI, per channel (ideal: R/B %.4f%%/%.4f%%, G %.4f%%/%.4f%%)\n\n', ...
    npcr_ideal_r, uaci_ideal_r, npcr_ideal_g, uaci_ideal_g);
fprintf(fid, '| Image | NPCR R %% | UACI R %% | NPCR G %% | UACI G %% | NPCR B %% | UACI B %% |\n');
fprintf(fid, '|---|---|---|---|---|---|---|\n');
for r = 1:size(summary_npcr_uaci, 1)
    fprintf(fid, '| %s | %.4f | %.4f | %.4f | %.4f | %.4f | %.4f |\n', summary_npcr_uaci{r, :});
end
fclose(fid);
fprintf('\nWrote results table to %s\n', md_path);

function plot_channel_histogram(img, nbits)
    maxv = 2^nbits - 1;
    counts = histcounts(double(img(:)), -0.5:1:(maxv + 0.5));
    bar(0:maxv, counts, 'BarWidth', 1, 'EdgeColor', 'none');
    xlim([0 maxv]);
    xlabel('Channel value'); ylabel('Count');
end
