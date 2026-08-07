% run_all.m
% Encrypts each downloaded test image with the xormap-based stream cipher
% (xormap_image_encrypt.m) and runs the standard image-cipher evaluation
% battery: round-trip correctness, histogram, adjacent-pixel correlation,
% entropy, and NPCR/UACI. Writes figures and a results table to results/.
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
    'name', {'boat.512 (Fishing Boat, 512x512)', ...
             '5.1.09 (Moon Surface, 256x256)', ...
             '7.1.01 (Truck, 512x512)'});

K = 512;  % 512/8 = 64 pixels of keystream per xormap_transform iteration
key_bits = secret_key(K);
NUM_CORR_SAMPLES = 5000;
NUM_SCATTER_SAMPLES = 3000;

rng(2026);  % reproducible adjacent-pixel sampling

summary = {};

for ii = 1:numel(images)
    fname = fullfile(this_dir, 'images', images(ii).file);
    if ~exist(fname, 'file')
        error('Image not found: %s. Run download_images.m first.', fname);
    end

    plain = imread(fname);
    if ndims(plain) > 2 || ~isa(plain, 'uint8')
        error('%s is not an 8-bit grayscale image', fname);
    end

    fprintf('\n=== %s ===\n', images(ii).name);

    % --- encrypt + round-trip correctness check ---
    [cipher, seed] = xormap_image_encrypt(plain, key_bits);
    recovered = xormap_image_decrypt(cipher, seed);
    assert(isequal(recovered, plain), 'round-trip failed for %s', images(ii).name);
    fprintf('Round-trip OK (decrypt(encrypt(P)) == P)\n');

    % --- NPCR/UACI: flip the LSB of the centre pixel of the plain image ---
    plain2 = plain;
    r0 = floor(size(plain, 1) / 2) + 1;
    c0 = floor(size(plain, 2) / 2) + 1;
    plain2(r0, c0) = bitxor(plain2(r0, c0), 1);
    cipher2 = xormap_image_encrypt(plain2, key_bits);
    [npcr, uaci] = npcr_uaci(cipher, cipher2);

    % --- entropy ---
    e_plain = shannon_entropy(plain);
    e_cipher = shannon_entropy(cipher);

    % --- adjacent-pixel correlation ---
    corrH_p = adjacent_correlation(plain, 'horizontal', NUM_CORR_SAMPLES);
    corrV_p = adjacent_correlation(plain, 'vertical', NUM_CORR_SAMPLES);
    corrD_p = adjacent_correlation(plain, 'diagonal', NUM_CORR_SAMPLES);
    corrH_c = adjacent_correlation(cipher, 'horizontal', NUM_CORR_SAMPLES);
    corrV_c = adjacent_correlation(cipher, 'vertical', NUM_CORR_SAMPLES);
    corrD_c = adjacent_correlation(cipher, 'diagonal', NUM_CORR_SAMPLES);

    fprintf('Entropy        plain=%.4f   cipher=%.4f   (ideal 8.0000)\n', e_plain, e_cipher);
    fprintf('Correlation H  plain=%+.4f  cipher=%+.4f\n', corrH_p, corrH_c);
    fprintf('Correlation V  plain=%+.4f  cipher=%+.4f\n', corrV_p, corrV_c);
    fprintf('Correlation D  plain=%+.4f  cipher=%+.4f\n', corrD_p, corrD_c);
    [npcr_ideal, uaci_ideal] = npcr_uaci_ideal(8);
    fprintf('NPCR = %.4f%%  (ideal %.4f%%)   UACI = %.4f%%  (ideal %.4f%%)\n', npcr, npcr_ideal, uaci, uaci_ideal);

    tag = erase(images(ii).file, '.tiff');

    % --- plain vs cipher image ---
    fh = figure('Visible', 'off', 'Position', [100 100 900 420]);
    subplot(1, 2, 1); imagesc(plain); axis image; colormap(gca, gray(256)); title('Plain'); colorbar;
    subplot(1, 2, 2); imagesc(cipher); axis image; colormap(gca, gray(256)); title('Cipher'); colorbar;
    sgtitle(images(ii).name, 'Interpreter', 'none');
    exportgraphics(fh, fullfile(results_dir, [tag '_images.pdf']), ...
        'ContentType', 'vector', 'BackgroundColor', 'white');
    close(fh);

    % --- histograms ---
    fh = figure('Visible', 'off', 'Position', [100 100 900 420]);
    subplot(1, 2, 1); plot_histogram(plain); title('Plain histogram');
    subplot(1, 2, 2); plot_histogram(cipher); title('Cipher histogram');
    sgtitle(images(ii).name, 'Interpreter', 'none');
    exportgraphics(fh, fullfile(results_dir, [tag '_histogram.pdf']), ...
        'ContentType', 'vector', 'BackgroundColor', 'white');
    close(fh);

    % --- adjacent-pixel scatter (horizontal direction) ---
    [~, xh_p, yh_p] = adjacent_correlation(plain, 'horizontal', NUM_SCATTER_SAMPLES);
    [~, xh_c, yh_c] = adjacent_correlation(cipher, 'horizontal', NUM_SCATTER_SAMPLES);
    fh = figure('Visible', 'off', 'Position', [100 100 900 420]);
    subplot(1, 2, 1); scatter(xh_p, yh_p, 4, 'filled'); axis([0 255 0 255]); axis square;
    xlabel('pixel(x,y)'); ylabel('pixel(x,y+1)'); title('Plain (horizontal)');
    subplot(1, 2, 2); scatter(xh_c, yh_c, 4, 'filled'); axis([0 255 0 255]); axis square;
    xlabel('pixel(x,y)'); ylabel('pixel(x,y+1)'); title('Cipher (horizontal)');
    sgtitle(images(ii).name, 'Interpreter', 'none');
    exportgraphics(fh, fullfile(results_dir, [tag '_correlation.pdf']), ...
        'ContentType', 'vector', 'BackgroundColor', 'white');
    close(fh);

    summary(end + 1, :) = {images(ii).name, e_plain, e_cipher, ...
        corrH_p, corrH_c, corrV_p, corrV_c, corrD_p, corrD_c, npcr, uaci}; %#ok<SAGROW>
end

% --- results table ---
md_path = fullfile(results_dir, 'results.md');
fid = fopen(md_path, 'w');
fprintf(fid, '| Image | Entropy (plain) | Entropy (cipher) | Corr-H (plain) | Corr-H (cipher) | Corr-V (plain) | Corr-V (cipher) | Corr-D (plain) | Corr-D (cipher) | NPCR %% | UACI %% |\n');
fprintf(fid, '|---|---|---|---|---|---|---|---|---|---|---|\n');
for r = 1:size(summary, 1)
    fprintf(fid, '| %s | %.4f | %.4f | %+.4f | %+.4f | %+.4f | %+.4f | %+.4f | %+.4f | %.4f | %.4f |\n', summary{r, :});
end
fclose(fid);
fprintf('\nWrote results table to %s\n', md_path);

function plot_histogram(img)
    counts = histcounts(double(img(:)), -0.5:1:255.5);
    bar(0:255, counts, 'BarWidth', 1, 'EdgeColor', 'none');
    xlim([0 255]);
    xlabel('Pixel value'); ylabel('Count');
end
