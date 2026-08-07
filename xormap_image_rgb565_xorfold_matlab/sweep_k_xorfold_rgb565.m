% sweep_k_xorfold_rgb565.m
% Run the one-xormap-state-per-RGB565-pixel XOR-fold experiment for the
% exact requested widths K = 32:32:512. Each encryption performs
% numel(image) PRNG iterations regardless of K. A second encryption of a
% one-bit-perturbed plaintext is generated for NPCR/UACI, so the reported
% time and total_iterations cover two encryptions.
%
% Parallelized across a local pool sized to this machine's logical core
% count (`nproc`, not MATLAB's feature('numcores') which reports physical
% cores only) with one (image, K) pair per task -- 3 x 16 = 48 tasks --
% the same pattern used by sweep_k_xorfold_rgb888.m.

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
for ii = 1:numel(images)
    fname = fullfile(rgb565_dir, 'images', images(ii).file);
    if ~exist(fname, 'file')
        error('Image not found: %s. Run xormap_image_rgb565_matlab/download_images.m first.', fname);
    end
end

K_VALUES = 32:32:512;
NUM_CORR_SAMPLES = 3000;
NBITS = [5, 6, 5];

[npcr_ideal16, uaci_ideal16] = npcr_uaci_ideal(16);

num_images = numel(images);
[II, KK] = ndgrid(1:num_images, K_VALUES);
task_img = II(:);
task_K = KK(:);
num_tasks = numel(task_img);
fprintf('%d images x %d K values = %d tasks\n', num_images, numel(K_VALUES), num_tasks);

start_pool();

% columns: K, height, width, image_size_pixels, iterations_per_encrypt,
%          fold_chunks, norm_entropy, corrH, npcr, uaci, seconds
results_numeric = zeros(num_tasks, 11);

sweep_tic = tic;
parfor idx = 1:num_tasks
    img = images(task_img(idx));
    K = task_K(idx);

    rgb = imread(fullfile(rgb565_dir, 'images', img.file));
    plain = rgb888_to_rgb565(rgb);
    image_size_pixels = numel(plain);

    plain2 = plain;
    r0 = floor(size(plain, 1) / 2) + 1;
    c0 = floor(size(plain, 2) / 2) + 1;
    plain2(r0, c0) = bitxor(plain2(r0, c0), uint16(1));

    key_bits = secret_key(K);

    t0 = tic;
    [cipher, ~, iterations1] = xormap_rgb565_xorfold_encrypt(plain, key_bits);
    [cipher2, ~, iterations2] = xormap_rgb565_xorfold_encrypt(plain2, key_bits);
    elapsed = toc(t0);

    assert(iterations1 == image_size_pixels && iterations2 == image_size_pixels, ...
        'iteration count mismatch for K=%d', K);

    rng(idx);  % deterministic per-task seed despite parfor's task scheduling

    [Rc, Gc, Bc] = rgb565_to_channels(cipher);
    cipher_ch = {Rc, Gc, Bc};
    norm_entropy = mean(arrayfun(@(c) ...
        shannon_entropy(cipher_ch{c}, NBITS(c)) / NBITS(c), 1:3));
    corrH = mean(arrayfun(@(c) abs(adjacent_correlation( ...
        cipher_ch{c}, 'horizontal', NUM_CORR_SAMPLES)), 1:3));
    [npcr, uaci] = npcr_uaci(cipher, cipher2, 65535);

    results_numeric(idx, :) = [K, size(plain, 1), size(plain, 2), ...
        image_size_pixels, iterations1, K / 16, norm_entropy, corrH, npcr, uaci, elapsed];
end
total_elapsed = toc(sweep_tic);
fprintf('Sweep finished in %.1f s (%.1f min).\n', total_elapsed, total_elapsed / 60);

% --- CSV, grouped by image then K to match the original layout ---
csv_path = fullfile(results_dir, 'sweep_k_xorfold_rgb565.csv');
fid = fopen(csv_path, 'w');
if fid < 0
    error('Could not open %s for writing', csv_path);
end
fprintf(fid, ['image,K,height,width,image_size_pixels,iterations_per_encrypt,' ...
    'total_iterations_two_encryptions,pixels_per_iteration,fold_chunks,' ...
    'mean_norm_entropy_cipher,mean_abs_corrH_cipher,npcr_packed,uaci_packed,' ...
    'seconds_two_encryptions\n']);
order = [];
for ii = 1:num_images
    order = [order, find(task_img == ii)']; %#ok<AGROW>
end
for r = order
    row = results_numeric(r, :);
    fprintf(fid, '%s,%d,%d,%d,%d,%d,%d,%d,%d,%.6f,%.6f,%.6f,%.6f,%.4f\n', ...
        images(task_img(r)).name, row(1), row(2), row(3), row(4), row(5), ...
        2 * row(5), 1, row(6), row(7), row(8), row(9), row(10), row(11));
end
fclose(fid);
fprintf('\nWrote %s\n', csv_path);

% --- plot: per-image lines over K ---
names = {images.name};
colors = lines(numel(names));
metrics = { ...
    7, 'Mean normalized entropy (of max)', 1.0; ...
    8, 'Mean |horizontal correlation|', 0.0; ...
    9, 'NPCR, packed word (%)', npcr_ideal16; ...
    10, 'UACI, packed word (%)', uaci_ideal16; ...
    11, 'Time for two encryptions (s)', NaN};

fh = figure('Visible', 'off', 'Position', [100 100 1100 780]);
for m = 1:size(metrics, 1)
    subplot(3, 2, m); hold on;
    col = metrics{m, 1};
    for ii = 1:numel(names)
        selected = (task_img == ii);
        k_col = results_numeric(selected, 1);
        v_col = results_numeric(selected, col);
        [k_col, order_ii] = sort(k_col);
        v_col = v_col(order_ii);
        plot(k_col, v_col, '-o', 'Color', colors(ii, :), 'DisplayName', names{ii});
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
pdf_path = fullfile(results_dir, 'sweep_k_xorfold_rgb565.pdf');
exportgraphics(fh, pdf_path, 'ContentType', 'vector', 'BackgroundColor', 'white');
close(fh);
fprintf('Wrote %s\n', png_path);
fprintf('Wrote %s\n', pdf_path);

function start_pool()
% Size the pool to the machine's logical core count (nproc), not just
% MATLAB's feature('numcores') (physical cores only) -- this host reports
% 12 physical / 24 logical, and the (image, K) task split benefits from
% every logical core.
    n = local_nproc();
    pool = gcp('nocreate');
    if isempty(pool) || pool.NumWorkers ~= n
        if ~isempty(pool)
            delete(pool);
        end
        c = parcluster('local');
        if c.NumWorkers < n
            c.NumWorkers = n;
            saveProfile(c);
        end
        pool = parpool('local', n);
    end
    fprintf('Using parallel pool with %d workers.\n', pool.NumWorkers);
end

function n = local_nproc()
    n = feature('numcores');
    if isunix
        [status, out] = system('nproc');
        if status == 0
            parsed = str2double(strtrim(out));
            if isfinite(parsed) && parsed > 0
                n = parsed;
            end
        end
    end
end
