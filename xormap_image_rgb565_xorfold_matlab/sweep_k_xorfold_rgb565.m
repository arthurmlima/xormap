% sweep_k_xorfold_rgb565.m
% Run the one-xormap-state-per-RGB565-pixel XOR-fold experiment for the
% exact requested widths K = 32:32:512.  Each encryption performs
% numel(image) PRNG iterations regardless of K.  A second encryption of a
% one-bit-perturbed plaintext is generated for NPCR/UACI, so the reported
% time and total_iterations cover two encryptions.

clear all;
clc

this_dir = fileparts(mfilename('fullpath'));
rgb565_dir = fullfile(this_dir, '..', 'xormap_image_rgb565_matlab');
addpath(this_dir);
addpath(fullfile(this_dir, '..', 'xormap_matlab'));
addpath(fullfile(this_dir, '..', 'xormap_image_matlab'));
addpath(rgb565_dir);

results_dir = fullfile(this_dir, 'results');
if ~exist(results_dir, 'dir')
    mkdir(results_dir);
end

images = struct( ...
    'file', {'4.2.03.tiff', '4.2.05.tiff', '4.2.07.tiff'}, ...
    'name', {'4.2.03 (Mandrill)', '4.2.05 (Airplane F-16)', '4.2.07 (Peppers)'});

K_VALUES = 32:32:512;
NUM_CORR_SAMPLES = 3000;
NBITS = [5, 6, 5];

[npcr_ideal16, uaci_ideal16] = npcr_uaci_ideal(16);
rng(2026);
rows = {};

for ii = 1:numel(images)
    fname = fullfile(rgb565_dir, 'images', images(ii).file);
    if ~exist(fname, 'file')
        error('Image not found: %s. Run xormap_image_rgb565_matlab/download_images.m first.', fname);
    end

    rgb = imread(fname);
    plain = rgb888_to_rgb565(rgb);
    image_size_pixels = numel(plain);

    plain2 = plain;
    r0 = floor(size(plain, 1) / 2) + 1;
    c0 = floor(size(plain, 2) / 2) + 1;
    plain2(r0, c0) = bitxor(plain2(r0, c0), uint16(1));

    fprintf('\n=== %s: %d x %d = %d pixels ===\n', ...
        images(ii).name, size(plain, 1), size(plain, 2), image_size_pixels);

    for K = K_VALUES
        key_bits = secret_key(K);

        t0 = tic;
        [cipher, ~, iterations1] = xormap_rgb565_xorfold_encrypt(plain, key_bits);
        [cipher2, ~, iterations2] = xormap_rgb565_xorfold_encrypt(plain2, key_bits);
        elapsed = toc(t0);

        assert(iterations1 == image_size_pixels && iterations2 == image_size_pixels, ...
            'iteration count mismatch for K=%d', K);

        [Rc, Gc, Bc] = rgb565_to_channels(cipher);
        cipher_ch = {Rc, Gc, Bc};
        norm_entropy = mean(arrayfun(@(c) ...
            shannon_entropy(cipher_ch{c}, NBITS(c)) / NBITS(c), 1:3));
        corrH = mean(arrayfun(@(c) abs(adjacent_correlation( ...
            cipher_ch{c}, 'horizontal', NUM_CORR_SAMPLES)), 1:3));
        [npcr, uaci] = npcr_uaci(cipher, cipher2, 65535);

        fprintf(['K=%3d  chunks/fold=%2d  pixels/iter=1  iterations/encrypt=%d  ' ...
            'norm.entropy=%.5f  mean|corrH|=%.4f  NPCR=%.4f  UACI=%.4f  t(2x)=%.3fs\n'], ...
            K, K / 16, iterations1, norm_entropy, corrH, npcr, uaci, elapsed);

        rows(end + 1, :) = {images(ii).name, K, size(plain, 1), size(plain, 2), ...
            image_size_pixels, iterations1, iterations1 + iterations2, 1, K / 16, ...
            norm_entropy, corrH, npcr, uaci, elapsed}; %#ok<SAGROW>
    end
end

csv_path = fullfile(results_dir, 'sweep_k_xorfold_rgb565.csv');
fid = fopen(csv_path, 'w');
if fid < 0
    error('Could not open %s for writing', csv_path);
end
fprintf(fid, ['image,K,height,width,image_size_pixels,iterations_per_encrypt,' ...
    'total_iterations_two_encryptions,pixels_per_iteration,fold_chunks,' ...
    'mean_norm_entropy_cipher,mean_abs_corrH_cipher,npcr_packed,uaci_packed,' ...
    'seconds_two_encryptions\n']);
for r = 1:size(rows, 1)
    fprintf(fid, '%s,%d,%d,%d,%d,%d,%d,%d,%d,%.6f,%.6f,%.6f,%.6f,%.4f\n', ...
        rows{r, :});
end
fclose(fid);
fprintf('\nWrote %s\n', csv_path);

names = {images.name};
colors = lines(numel(names));
metrics = { ...
    'mean_norm_entropy_cipher', 'Mean normalized entropy (of max)', 1.0, 10; ...
    'mean_abs_corrH_cipher', 'Mean |horizontal correlation|', 0.0, 11; ...
    'npcr_packed', 'NPCR, packed word (%)', npcr_ideal16, 12; ...
    'uaci_packed', 'UACI, packed word (%)', uaci_ideal16, 13; ...
    'seconds_two_encryptions', 'Time for two encryptions (s)', NaN, 14};

fh = figure('Visible', 'off', 'Position', [100 100 1100 780]);
for m = 1:size(metrics, 1)
    subplot(3, 2, m); hold on;
    for ii = 1:numel(names)
        selected = strcmp(rows(:, 1), names{ii});
        k_col = cell2mat(rows(selected, 2));
        v_col = cell2mat(rows(selected, metrics{m, 4}));
        plot(k_col, v_col, '-o', 'Color', colors(ii, :), ...
            'DisplayName', names{ii});
    end
    ideal = metrics{m, 3};
    if ~isnan(ideal)
        yline(ideal, '--k', 'ideal', 'HandleVisibility', 'off');
    end
    xlabel('K (bits)'); ylabel(metrics{m, 2}); title(metrics{m, 2}); grid on;
    if m == 1
        legend('Location', 'best');
    end
end
sgtitle('xormap RGB565: one K-bit state XOR-folded into one pixel (K=32:32:512)');
png_path = fullfile(results_dir, 'sweep_k_xorfold_rgb565.png');
saveas(fh, png_path);
close(fh);
fprintf('Wrote %s\n', png_path);
