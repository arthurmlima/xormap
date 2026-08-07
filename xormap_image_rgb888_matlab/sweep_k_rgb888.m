% sweep_k_rgb888.m
% Encrypts EVERY color image in the USC-SIPI database (51 images across
% the misc and aerials volumes, ~35M pixels total) with the non-fold
% RGB888 cipher, for every K = 24:24:384, and measures cipher entropy,
% adjacent-pixel correlation, and NPCR/UACI on the packed 24-bit word.
%
% This is a much larger job than the grayscale/RGB565 sweeps (51 images
% vs 3, and the two giant aerials -- 24 x 1024x1024 plus one 2250x2250 --
% dominate total pixel count), so it's parallelized across a 24-worker
% local pool: each (image, K) combination is one independent task
% (816 tasks total), which spreads even the largest single image's 16 K
% values across many workers instead of stranding them on one.
%
% Run download_images.m first if images/ isn't populated yet.

clear all;
clc

this_dir = fileparts(mfilename('fullpath'));
addpath(this_dir);
addpath(fullfile(this_dir, '..', 'xormap_matlab'));
addpath(fullfile(this_dir, '..', 'xormap_image_matlab'));
addpath(fullfile(this_dir, '..', 'xormap_image_rgb565_matlab'));       % bits_to_words

img_dir = fullfile(this_dir, 'images');
results_dir = fullfile(this_dir, 'results');
if ~exist(results_dir, 'dir')
    mkdir(results_dir);
end

manifest_path = fullfile(img_dir, 'manifest.csv');
if ~exist(manifest_path, 'file')
    error('Manifest not found: %s. Run download_images.m first.', manifest_path);
end
[image_files, image_names] = read_manifest(manifest_path, img_dir);

K_VALUES = 24:24:384;
NUM_CORR_SAMPLES = 3000;

num_images = numel(image_files);
num_K = numel(K_VALUES);
[II, KK] = ndgrid(1:num_images, K_VALUES);
task_image_idx = II(:);
task_K = KK(:);
num_tasks = numel(task_image_idx);

fprintf('%d images x %d K values = %d tasks\n', num_images, num_K, num_tasks);

% --- parallel pool ---
pool = gcp('nocreate');
if isempty(pool) || pool.NumWorkers < 24
    if ~isempty(pool)
        delete(pool);
    end
    c = parcluster('local');
    if c.NumWorkers < 24
        c.NumWorkers = 24;
        saveProfile(c);
    end
    pool = parpool('local', 24);
end
fprintf('Using parallel pool with %d workers.\n', pool.NumWorkers);

% columns: K, height, width, num_pixels, mean_entropy_cipher,
%          mean_abs_corrH_cipher, npcr_packed, uaci_packed, seconds
results_numeric = zeros(num_tasks, 9);

sweep_tic = tic;
parfor idx = 1:num_tasks
    fname = image_files{task_image_idx(idx)}; %#ok<PFBNS>
    K = task_K(idx);

    rgb = imread(fname);
    plain = rgb888_pack(rgb);
    [h, w] = size(plain);
    num_pixels = h * w;

    plain2 = plain;
    r0 = floor(h / 2) + 1;
    c0 = floor(w / 2) + 1;
    plain2(r0, c0) = bitxor(plain2(r0, c0), uint32(1));

    key_bits = secret_key(K);

    t0 = tic;
    cipher = xormap_rgb888_encrypt(plain, key_bits);
    cipher2 = xormap_rgb888_encrypt(plain2, key_bits);
    elapsed = toc(t0);

    % Deterministic per-task seed so correlation sampling is reproducible
    % regardless of how parfor schedules tasks across workers.
    rng(idx);

    cipher_rgb = rgb888_unpack(cipher);
    Rc = cipher_rgb(:, :, 1); Gc = cipher_rgb(:, :, 2); Bc = cipher_rgb(:, :, 3);

    e = mean([shannon_entropy(Rc), shannon_entropy(Gc), shannon_entropy(Bc)]);
    corrH = mean([ ...
        abs(adjacent_correlation(Rc, 'horizontal', NUM_CORR_SAMPLES)), ...
        abs(adjacent_correlation(Gc, 'horizontal', NUM_CORR_SAMPLES)), ...
        abs(adjacent_correlation(Bc, 'horizontal', NUM_CORR_SAMPLES))]);
    [npcr, uaci] = npcr_uaci(cipher, cipher2, 2^24 - 1);

    results_numeric(idx, :) = [K, h, w, num_pixels, e, corrH, npcr, uaci, elapsed];
end
total_elapsed = toc(sweep_tic);
fprintf('Sweep finished in %.1f s (%.1f min).\n', total_elapsed, total_elapsed / 60);

% --- CSV ---
csv_path = fullfile(results_dir, 'sweep_k_rgb888.csv');
fid = fopen(csv_path, 'w');
fprintf(fid, 'image,K,height,width,num_pixels,mean_entropy_cipher,mean_abs_corrH_cipher,npcr_packed,uaci_packed,seconds\n');
for r = 1:num_tasks
    fprintf(fid, '%s,%d,%d,%d,%d,%.6f,%.6f,%.6f,%.6f,%.4f\n', ...
        image_names{task_image_idx(r)}, results_numeric(r, :));
end
fclose(fid);
fprintf('Wrote %s\n', csv_path);

% --- plot: per-image points (faint) + per-K mean (bold), one panel per metric ---
[npcr_ideal24, uaci_ideal24] = npcr_uaci_ideal(24);

fh = figure('Visible', 'off', 'Position', [100 100 1100 780]);
metrics = { ...
    5, 'Mean entropy, cipher (bits)', 8.0; ...
    6, 'Mean |horizontal correlation|', 0.0; ...
    7, 'NPCR, packed word (%)', npcr_ideal24; ...
    8, 'UACI, packed word (%)', uaci_ideal24; ...
    9, 'Encrypt time, 2x per (image,K) (s)', NaN};

for m = 1:size(metrics, 1)
    subplot(3, 2, m); hold on;
    col = metrics{m, 1};
    scatter(results_numeric(:, 1), results_numeric(:, col), 6, [0.7 0.7 0.7], 'filled', 'MarkerFaceAlpha', 0.35);
    k_means = arrayfun(@(K) mean(results_numeric(results_numeric(:, 1) == K, col)), K_VALUES);
    plot(K_VALUES, k_means, '-o', 'Color', [0.85 0.33 0.10], 'LineWidth', 1.5, 'DisplayName', 'mean over 51 images');
    ideal = metrics{m, 3};
    if ~isnan(ideal)
        yline(ideal, '--k', 'ideal', 'HandleVisibility', 'off');
    end
    xlabel('K (bits)'); ylabel(metrics{m, 2}); title(metrics{m, 2}); grid on;
    if m == 1
        legend('Location', 'best');
    end
end
sgtitle(sprintf('xormap RGB888 (non-fold): every SIPI color image (%d), K=24:24:384', num_images));
pdf_path = fullfile(results_dir, 'sweep_k_rgb888.pdf');
exportgraphics(fh, pdf_path, 'ContentType', 'vector', 'BackgroundColor', 'white');
close(fh);
fprintf('Wrote %s\n', pdf_path);

function [files, names] = read_manifest(manifest_path, img_dir)
    fid = fopen(manifest_path);
    header = strsplit(fgetl(fid), ',');
    filename_col = find(strcmp(header, 'filename'));
    name_col = find(strcmp(header, 'name'));
    files = {};
    names = {};
    while ~feof(fid)
        line = fgetl(fid);
        if isempty(line)
            continue;
        end
        parts = strsplit(line, ',');
        files{end + 1} = fullfile(img_dir, parts{filename_col}); %#ok<AGROW>
        names{end + 1} = parts{name_col}; %#ok<AGROW>
    end
    fclose(fid);
end
