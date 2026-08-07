% sweep_k.m
% Sweeps the xormap keystream width K = 8:4:512 across the three test
% images and re-runs the cipher metrics at each K, to see whether a wider
% K actually improves the statistical test results (it mainly changes
% pixels-per-iteration = K/8 and per-iteration cost, not the seed quality,
% since the seed already comes from a full SHA-256-derived hash regardless
% of K). Writes results/sweep_k.csv and results/sweep_k.pdf.
%
% Run download_images.m first if images/*.tiff aren't there yet.

clear all;
clc

this_dir = fileparts(mfilename('fullpath'));
addpath(this_dir);
addpath(fullfile(this_dir, '..', 'xormap_matlab'));

results_dir = fullfile(this_dir, 'results');
if ~exist(results_dir, 'dir')
    mkdir(results_dir);
end

images = struct( ...
    'file', {'boat.512.tiff', '5.1.09.tiff', '7.1.01.tiff'}, ...
    'name', {'boat.512', '5.1.09', '7.1.01'});

K_VALUES = 8:4:512;
NUM_CORR_SAMPLES = 3000;

rng(2026);

rows = {};

for ii = 1:numel(images)
    fname = fullfile(this_dir, 'images', images(ii).file);
    if ~exist(fname, 'file')
        error('Image not found: %s. Run download_images.m first.', fname);
    end
    plain = imread(fname);

    plain2 = plain;
    r0 = floor(size(plain, 1) / 2) + 1;
    c0 = floor(size(plain, 2) / 2) + 1;
    plain2(r0, c0) = bitxor(plain2(r0, c0), 1);

    fprintf('\n=== %s ===\n', images(ii).name);

    for K = K_VALUES
        key_bits = secret_key(K);

        t0 = tic;
        cipher = xormap_image_encrypt(plain, key_bits);
        cipher2 = xormap_image_encrypt(plain2, key_bits);
        elapsed = toc(t0);

        e_c = shannon_entropy(cipher);
        corrH = adjacent_correlation(cipher, 'horizontal', NUM_CORR_SAMPLES);
        corrV = adjacent_correlation(cipher, 'vertical', NUM_CORR_SAMPLES);
        corrD = adjacent_correlation(cipher, 'diagonal', NUM_CORR_SAMPLES);
        [npcr, uaci] = npcr_uaci(cipher, cipher2);

        fprintf('K=%3d  px/iter=%5.1f  entropy=%.4f  |corrH|=%.4f  |corrV|=%.4f  |corrD|=%.4f  NPCR=%.4f  UACI=%.4f  t=%.3fs\n', ...
            K, K / 8, e_c, abs(corrH), abs(corrV), abs(corrD), npcr, uaci, elapsed);

        rows(end + 1, :) = {images(ii).name, K, e_c, corrH, corrV, corrD, npcr, uaci, elapsed}; %#ok<SAGROW>
    end
end

% --- CSV ---
csv_path = fullfile(results_dir, 'sweep_k.csv');
fid = fopen(csv_path, 'w');
fprintf(fid, 'image,K,entropy_cipher,corrH_cipher,corrV_cipher,corrD_cipher,npcr,uaci,seconds\n');
for r = 1:size(rows, 1)
    fprintf(fid, '%s,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.4f\n', rows{r, :});
end
fclose(fid);
fprintf('\nWrote %s\n', csv_path);

% --- plot: one line per image, metric vs K ---
names = {images.name};
colors = lines(numel(names));

fh = figure('Visible', 'off', 'Position', [100 100 1100 780]);

[npcr_ideal8, uaci_ideal8] = npcr_uaci_ideal(8);
metrics = {'entropy_cipher', 'Entropy (bits)', 8.0; ...
           'corrH_cipher',   'Horizontal correlation', 0.0; ...
           'npcr',           'NPCR (%)', npcr_ideal8; ...
           'uaci',           'UACI (%)', uaci_ideal8; ...
           'seconds',        'Encrypt time, 2x per K (s)', NaN};

col_idx = struct('entropy_cipher', 3, 'corrH_cipher', 4, 'npcr', 7, 'uaci', 8, 'seconds', 9);

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

sgtitle('xormap image cipher: metric vs keystream width K (8:4:512)');
exportgraphics(fh, fullfile(results_dir, 'sweep_k.pdf'), ...
    'ContentType', 'vector', 'BackgroundColor', 'white');
close(fh);
fprintf('Wrote %s\n', fullfile(results_dir, 'sweep_k.pdf'));
