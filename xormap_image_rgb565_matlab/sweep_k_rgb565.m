% sweep_k_rgb565.m
% RGB565 analogue of xormap_image_matlab/sweep_k.m: sweeps the xormap
% keystream width K = 8:4:512 across the three color test images and
% re-runs the cipher metrics at each K. Writes results/sweep_k_rgb565.csv
% and results/sweep_k_rgb565.pdf.
%
% Run download_images.m first if images/*.tiff aren't there yet.

clear all;
clc

this_dir = fileparts(mfilename('fullpath'));
addpath(this_dir);
addpath(fullfile(this_dir, '..', 'xormap_matlab'));
addpath(fullfile(this_dir, '..', 'xormap_image_matlab'));

results_dir = fullfile(this_dir, 'results');
if ~exist(results_dir, 'dir')
    mkdir(results_dir);
end

images = struct( ...
    'file', {'4.2.03.tiff', '4.2.05.tiff', '4.2.07.tiff'}, ...
    'name', {'4.2.03 (Mandrill)', '4.2.05 (Airplane F-16)', '4.2.07 (Peppers)'});

K_VALUES = 8:4:512;
NUM_CORR_SAMPLES = 3000;
NBITS = [5, 6, 5];

[npcr_ideal16, uaci_ideal16] = npcr_uaci_ideal(16);

rng(2026);

rows = {};

for ii = 1:numel(images)
    fname = fullfile(this_dir, 'images', images(ii).file);
    if ~exist(fname, 'file')
        error('Image not found: %s. Run download_images.m first.', fname);
    end
    rgb = imread(fname);
    plain = rgb888_to_rgb565(rgb);

    plain2 = plain;
    r0 = floor(size(plain, 1) / 2) + 1;
    c0 = floor(size(plain, 2) / 2) + 1;
    plain2(r0, c0) = bitxor(plain2(r0, c0), uint16(1));

    fprintf('\n=== %s ===\n', images(ii).name);

    for K = K_VALUES
        key_bits = secret_key(K);

        t0 = tic;
        cipher = xormap_rgb565_encrypt(plain, key_bits);
        cipher2 = xormap_rgb565_encrypt(plain2, key_bits);
        elapsed = toc(t0);

        [Rc, Gc, Bc] = rgb565_to_channels(cipher);
        cipher_ch = {Rc, Gc, Bc};

        norm_entropy = mean(arrayfun(@(c) shannon_entropy(cipher_ch{c}, NBITS(c)) / NBITS(c), 1:3));
        corrH = mean(arrayfun(@(c) abs(adjacent_correlation(cipher_ch{c}, 'horizontal', NUM_CORR_SAMPLES)), 1:3));
        [npcr, uaci] = npcr_uaci(cipher, cipher2, 65535);

        fprintf('K=%3d  px/iter=%5.1f  norm.entropy=%.5f  mean|corrH|=%.4f  NPCR=%.4f (ideal %.4f)  UACI=%.4f (ideal %.4f)  t=%.3fs\n', ...
            K, K / 16, norm_entropy, corrH, npcr, npcr_ideal16, uaci, uaci_ideal16, elapsed);

        rows(end + 1, :) = {images(ii).name, K, norm_entropy, corrH, npcr, uaci, elapsed}; %#ok<SAGROW>
    end
end

% --- CSV ---
csv_path = fullfile(results_dir, 'sweep_k_rgb565.csv');
fid = fopen(csv_path, 'w');
fprintf(fid, 'image,K,mean_norm_entropy_cipher,mean_abs_corrH_cipher,npcr_packed,uaci_packed,seconds\n');
for r = 1:size(rows, 1)
    fprintf(fid, '%s,%d,%.6f,%.6f,%.6f,%.6f,%.4f\n', rows{r, :});
end
fclose(fid);
fprintf('\nWrote %s\n', csv_path);

% --- plot: one line per image, metric vs K ---
names = {images.name};
colors = lines(numel(names));

fh = figure('Visible', 'off', 'Position', [100 100 1100 780]);

metrics = {'mean_norm_entropy_cipher', 'Mean normalized entropy (of max)', 1.0; ...
           'mean_abs_corrH_cipher',    'Mean |horizontal correlation|', 0.0; ...
           'npcr_packed',              'NPCR, packed word (%)', npcr_ideal16; ...
           'uaci_packed',              'UACI, packed word (%)', uaci_ideal16; ...
           'seconds',                  'Encrypt time, 2x per K (s)', NaN};

col_idx = struct('mean_norm_entropy_cipher', 3, 'mean_abs_corrH_cipher', 4, 'npcr_packed', 5, 'uaci_packed', 6, 'seconds', 7);

for m = 1:size(metrics, 1)
    subplot(3, 2, m); hold on;
    field = metrics{m, 1};
    for ii = 1:numel(names)
        sel = strcmp(rows(:, 1), names{ii});
        k_col = cell2mat(rows(sel, 2));
        v_col = cell2mat(rows(sel, col_idx.(field)));
        [k_col, order] = sort(k_col);
        v_col = v_col(order);
        plot(k_col, v_col, '-o', 'Color', colors(ii, :), 'DisplayName', names{ii});
    end
    ideal = metrics{m, 3};
    if ~isnan(ideal)
        yline(ideal, '--k', 'ideal', 'HandleVisibility', 'off');
    end
    xlabel('K (bits)'); ylabel(metrics{m, 2}); title(metrics{m, 2});
    if m == 1
        legend('Location', 'southeast');
    end
    grid on;
end

sgtitle('xormap RGB565 image cipher: metric vs keystream width K (8:4:512)');
exportgraphics(fh, fullfile(results_dir, 'sweep_k_rgb565.pdf'), ...
    'ContentType', 'vector', 'BackgroundColor', 'white');
close(fh);
fprintf('Wrote %s\n', fullfile(results_dir, 'sweep_k_rgb565.pdf'));
