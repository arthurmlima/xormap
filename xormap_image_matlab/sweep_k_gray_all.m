% sweep_k_gray_all.m
% Runs the standard cipher metrics across EVERY grayscale image the
% USC-SIPI database currently distributes (159 images, ~51.7M pixels; see
% download_images_all_gray.m) for K = 24:24:384.
%
% This is the grayscale counterpart of
% xormap_image_rgb888_matlab/sweep_k_rgb888.m, and deliberately uses that
% sweep's K grid rather than the older 3-image sweep_k.m's K = 8:4:512, so
% the two can be plotted against each other at matching K
% (compare_rgb888_vs_gray.m). Every K here is a multiple of 8, so the
% keystream's K-bit iterations tile the 8-bit pixel grid exactly.
%
% Cost note: one iteration yields K/8 pixels, so iterations per encryption
% = ceil(N*8/K) and the work SHRINKS as K grows. Total across the sweep is
% ~117M state updates, cheap enough that the fast transform makes this a
% couple of minutes.
%
% Run download_images_all_gray.m first if images/manifest_gray.csv isn't
% there yet.

clear all;
clc

this_dir = fileparts(mfilename('fullpath'));
addpath(this_dir);
addpath(fullfile(this_dir, '..', 'xormap_matlab'));

img_dir = fullfile(this_dir, 'images');
results_dir = fullfile(this_dir, 'results');
if ~exist(results_dir, 'dir')
    mkdir(results_dir);
end

manifest_path = fullfile(img_dir, 'manifest_gray.csv');
if ~exist(manifest_path, 'file')
    error('Manifest not found: %s. Run download_images_all_gray.m first.', manifest_path);
end
[image_files, image_names, image_vols] = read_manifest(manifest_path, img_dir);

K_VALUES = 24:24:384;
NUM_CORR_SAMPLES = 3000;

num_images = numel(image_files);
num_K = numel(K_VALUES);
[II, KK] = ndgrid(1:num_images, K_VALUES);
task_image_idx = II(:);
task_K = KK(:);
num_tasks = numel(task_image_idx);

fprintf('%d images x %d K values = %d tasks\n', num_images, num_K, num_tasks);

% --- parallel pool, sized to this machine ---
num_workers = min(feature('numcores'), num_tasks);
pool = gcp('nocreate');
if isempty(pool) || pool.NumWorkers ~= num_workers
    if ~isempty(pool)
        delete(pool);
    end
    c = parcluster('local');
    if c.NumWorkers < num_workers
        c.NumWorkers = num_workers;
        saveProfile(c);
    end
    pool = parpool('local', num_workers);
end
fprintf('Using parallel pool with %d workers.\n', pool.NumWorkers);

% columns: K, height, width, num_pixels, iterations_per_encrypt,
%          pixels_per_iteration, entropy_cipher, abs_corrH_cipher,
%          npcr, uaci, seconds_two_encryptions
results_numeric = zeros(num_tasks, 11);

sweep_tic = tic;
parfor idx = 1:num_tasks
    fname = image_files{task_image_idx(idx)}; %#ok<PFBNS>
    K = task_K(idx);

    plain = imread(fname);
    assert(ismatrix(plain) && isa(plain, 'uint8'), ...
        '%s is not a 2-D uint8 grayscale image', fname);
    [h, w] = size(plain);
    num_pixels = h * w;

    plain2 = plain;
    r0 = floor(h / 2) + 1;
    c0 = floor(w / 2) + 1;
    plain2(r0, c0) = bitxor(plain2(r0, c0), uint8(1));

    key_bits = secret_key(K);

    t0 = tic;
    [cipher,  ~, iterations1] = xormap_image_encrypt_fast(plain,  key_bits);
    [cipher2, ~, iterations2] = xormap_image_encrypt_fast(plain2, key_bits);
    elapsed = toc(t0);

    assert(iterations1 == iterations2 && iterations1 == ceil(num_pixels * 8 / K), ...
        'iteration count mismatch for %s K=%d', fname, K);

    % Deterministic per-task seed so the correlation sampling is
    % reproducible regardless of how parfor schedules tasks.
    rng(idx);

    e = shannon_entropy(cipher);
    corrH = abs(adjacent_correlation(cipher, 'horizontal', NUM_CORR_SAMPLES));
    [npcr, uaci] = npcr_uaci(cipher, cipher2);

    results_numeric(idx, :) = [K, h, w, num_pixels, iterations1, K / 8, ...
        e, corrH, npcr, uaci, elapsed];
end
total_elapsed = toc(sweep_tic);
fprintf('Sweep finished in %.1f s (%.1f min).\n', total_elapsed, total_elapsed / 60);

% --- CSV ---
csv_path = fullfile(results_dir, 'sweep_k_gray_all.csv');
fid = fopen(csv_path, 'w');
fprintf(fid, ['image,volume,K,height,width,num_pixels,iterations_per_encrypt,' ...
    'pixels_per_iteration,entropy_cipher,abs_corrH_cipher,npcr,uaci,' ...
    'seconds_two_encryptions\n']);
for r = 1:num_tasks
    fprintf(fid, '%s,%s,%d,%d,%d,%d,%d,%d,%.6f,%.6f,%.6f,%.6f,%.4f\n', ...
        image_names{task_image_idx(r)}, image_vols{task_image_idx(r)}, ...
        results_numeric(r, :));
end
fclose(fid);
fprintf('Wrote %s\n', csv_path);

% --- plot: per-image points (faint) + per-K mean (bold) ---
[npcr_ideal8, uaci_ideal8] = npcr_uaci_ideal(8);

fh = figure('Visible', 'off', 'Position', [100 100 1100 780]);
try %#ok<TRYNC>
    theme(fh, 'light');
end
metrics = { ...
    7,  'Entropy, cipher (bits)', 8.0; ...
    8,  'Mean |horizontal correlation|', 0.0; ...
    9,  'NPCR (%)', npcr_ideal8; ...
    10, 'UACI (%)', uaci_ideal8; ...
    11, 'Time for two encryptions (s)', NaN};

for m = 1:size(metrics, 1)
    subplot(3, 2, m); hold on;
    col = metrics{m, 1};
    scatter(results_numeric(:, 1), results_numeric(:, col), 6, [0.7 0.7 0.7], ...
        'filled', 'MarkerFaceAlpha', 0.35);
    k_means = arrayfun(@(K) mean(results_numeric(results_numeric(:, 1) == K, col)), K_VALUES);
    plot(K_VALUES, k_means, '-o', 'Color', [0.85 0.33 0.10], 'LineWidth', 1.5, ...
        'DisplayName', sprintf('mean over %d images', num_images));
    ideal = metrics{m, 3};
    if ~isnan(ideal)
        yline(ideal, '--k', 'ideal', 'HandleVisibility', 'off');
    end
    xlabel('K (bits)'); ylabel(metrics{m, 2}); title(metrics{m, 2}); grid on;
    if m == 1
        legend('Location', 'best');
    end
end
sgtitle(sprintf(['xormap grayscale: every SIPI grayscale image (%d), ' ...
    'K=24:24:384'], num_images));
pdf_path = fullfile(results_dir, 'sweep_k_gray_all.pdf');
exportgraphics(fh, pdf_path, 'ContentType', 'vector', 'BackgroundColor', 'white');
close(fh);
fprintf('Wrote %s\n', pdf_path);

function [files, names, vols] = read_manifest(manifest_path, img_dir)
    fid = fopen(manifest_path);
    header = strsplit(fgetl(fid), ',');
    filename_col = find(strcmp(header, 'filename'));
    name_col = find(strcmp(header, 'name'));
    vol_col = find(strcmp(header, 'volume'));
    files = {};
    names = {};
    vols = {};
    while ~feof(fid)
        line = fgetl(fid);
        if isempty(line)
            continue;
        end
        parts = strsplit(line, ',');
        files{end + 1} = fullfile(img_dir, parts{filename_col}); %#ok<AGROW>
        names{end + 1} = parts{name_col}; %#ok<AGROW>
        vols{end + 1} = parts{vol_col}; %#ok<AGROW>
    end
    fclose(fid);
end
