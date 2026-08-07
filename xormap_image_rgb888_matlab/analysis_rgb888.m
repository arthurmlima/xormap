function analysis_rgb888()
%ANALYSIS_RGB888 Key sensitivity, histogram and PSNR analysis, RGB888.
%   The colour counterpart of ../xormap_image_matlab/analysis_gray.m: runs
%   the same three tests across every SIPI colour image (51, see
%   download_images.m) at K = 24:24:384, writing one CSV and one vector PDF
%   per test into results/:
%
%     key_sensitivity_rgb888.{csv,pdf}    NPCR/UACI/PSNR between two
%                                         ciphers whose keys differ in ONE
%                                         bit, the PSNR of a wrong-key
%                                         decryption, and a per-bit study.
%     histogram_analysis_rgb888.{csv,pdf} chi-square uniformity of the R,
%                                         G and B histograms.
%     psnr_analysis_rgb888.{csv,pdf}      PSNR(plain, cipher) and the
%                                         round-trip PSNR.
%
%   All three share the same two encryptions per (image, K), so they run in
%   ONE pass rather than three.
%
%   WHAT IS MEASURED ON WHAT. Entropy-style per-channel quantities (the
%   chi-square histogram test, and PSNR) are computed on the three standard
%   8-bit channels from rgb888_unpack, so the ideals are the ordinary 8-bit
%   ones and PSNR uses peak 255 over the full RGB triple. NPCR/UACI are
%   computed on the packed 24-bit word (L=2^24), matching sweep_k_rgb888.m,
%   so their ideals come from npcr_uaci_ideal(24) -- NOT the 8-bit values.
%
%   IS PSNR APPLICABLE? PSNR(plain, cipher) and PSNR(plain, wrong-key
%   decryption) are meaningful and reported; both should be LOW, the
%   inverse of PSNR's usual fidelity role. The round-trip PSNR is exactly
%   +Inf by construction -- a pure XOR stream cipher is lossless, with no
%   quantisation, so MSE is identically zero. It is asserted as a
%   correctness check and carries no tuning information.
%
%   KEY SENSITIVITY OF A LINEAR MAP. xormap_transform is linear over GF(2),
%   so with S1 = key1 XOR h(P) and S2 = key2 XOR h(P):
%
%       C1 XOR C2 = KS(S1) XOR KS(S2) = KS(S1 XOR S2) = KS(key1 XOR key2)
%
%   i.e. the ciphertext difference depends only on which key bit flipped,
%   not on the image. That prediction is tested here rather than assumed
%   (the difference is checksummed in row-major keystream order -- column
%   order would compare different keystream positions across images of
%   different widths and fail spuriously).

    this_dir = fileparts(mfilename('fullpath'));
    addpath(this_dir);
    addpath(fullfile(this_dir, '..', 'xormap_matlab'));
    addpath(fullfile(this_dir, '..', 'xormap_image_matlab'));      % metrics
    addpath(fullfile(this_dir, '..', 'xormap_image_rgb565_matlab'));% bits_to_words

    img_dir = fullfile(this_dir, 'images');
    results_dir = fullfile(this_dir, 'results');
    if ~exist(results_dir, 'dir')
        mkdir(results_dir);
    end

    manifest_path = fullfile(img_dir, 'manifest.csv');
    if ~exist(manifest_path, 'file')
        error('analysis_rgb888:noManifest', ...
            'Manifest not found: %s. Run download_images.m first.', manifest_path);
    end
    [files, names, vols] = read_manifest(manifest_path, img_dir);

    K_VALUES = 24:24:384;
    FLIP_BIT = 1;

    num_images = numel(files);
    [II, KK] = ndgrid(1:num_images, K_VALUES);
    task_img = II(:);
    task_K = KK(:);
    num_tasks = numel(task_img);

    fprintf('%d images x %d K values = %d tasks\n', num_images, numel(K_VALUES), num_tasks);
    start_pool();

    % columns: K, num_pixels, npcr_key, uaci_key, psnr_c1c2,
    %          psnr_plain_wrongkey, chi2_plain, chi2_cipher, chi2_critical,
    %          psnr_plain_cipher, psnr_roundtrip, diff_checksum,
    %          first_differing_word
    R = zeros(num_tasks, 13);

    t_all = tic;
    parfor idx = 1:num_tasks
        fname = files{task_img(idx)}; %#ok<PFBNS>
        K = task_K(idx);

        rgb = imread(fname);
        plain = rgb888_pack(rgb);
        num_pixels = numel(plain);

        key1 = secret_key(K);
        key2 = key1;
        key2(FLIP_BIT) = ~key2(FLIP_BIT);

        [c1, seed1] = xormap_rgb888_encrypt(plain, key1);
        c2          = xormap_rgb888_encrypt(plain, key2);

        cipher_diff = bitxor(c1, c2);

        % Row-major: the keystream is laid out that way, and (:) is
        % column-major, which would compare different keystream positions
        % across images of different widths.
        diff_row = reshape(cipher_diff.', 1, []);
        first_diff = find(diff_row ~= 0, 1, 'first');
        if isempty(first_diff)
            first_diff = 0;
        end

        % Wrong-key decryption, free from XOR algebra:
        %   decrypt(C1, S2) = P XOR (C1 XOR C2)
        % Task 1 checks that against the real decrypt rather than trusting it.
        wrong = bitxor(plain, cipher_diff);
        if idx == 1
            [~, seed2] = xormap_rgb888_encrypt(plain, key2);
            assert(isequal(xormap_rgb888_decrypt(c1, seed2), wrong), ...
                'wrong-key shortcut disagrees with decrypt()');
        end

        recovered = xormap_rgb888_decrypt(c1, seed1);
        assert(isequal(recovered, plain), 'round trip failed for %s K=%d', fname, K);

        % NPCR/UACI on the packed 24-bit word, as in sweep_k_rgb888.m
        [npcr_key, uaci_key] = npcr_uaci(c1, c2, 2^24 - 1);

        % Histogram and PSNR on the three standard 8-bit channels
        plain_rgb  = rgb888_unpack(plain);
        cipher_rgb = rgb888_unpack(c1);
        wrong_rgb  = rgb888_unpack(wrong);
        c2_rgb     = rgb888_unpack(c2);

        chi2_p = 0; chi2_c = 0; chi2_crit = 0;
        for ch = 1:3
            [a, ~, crit] = chi_square_uniformity(plain_rgb(:, :, ch));
            b = chi_square_uniformity(cipher_rgb(:, :, ch));
            chi2_p = chi2_p + a / 3;
            chi2_c = chi2_c + b / 3;
            chi2_crit = crit;
        end

        R(idx, :) = [K, num_pixels, npcr_key, uaci_key, ...
            psnr_db(cipher_rgb, c2_rgb), psnr_db(plain_rgb, wrong_rgb), ...
            chi2_p, chi2_c, chi2_crit, ...
            psnr_db(plain_rgb, cipher_rgb), psnr_db(plain, recovered), ...
            diff_checksum(diff_row), first_diff];
    end
    fprintf('Main pass finished in %.1f s.\n', toc(t_all));

    same_per_K = arrayfun(@(K) numel(unique(R(R(:,1) == K, 12))) == 1, K_VALUES);
    fprintf(['Key-difference independent of image: %d of %d K values ' ...
             '(prediction: all %d).\n'], sum(same_per_K), numel(K_VALUES), numel(K_VALUES));

    B = bit_position_study(files{1}, K_VALUES);

    write_key_sensitivity(results_dir, R, B, names, vols, task_img, K_VALUES, ...
        FLIP_BIT, same_per_K);
    write_histogram(results_dir, R, names, vols, task_img, K_VALUES, files);
    write_psnr(results_dir, R, names, vols, task_img, K_VALUES);
end

% ---------------------------------------------------------------------------

function B = bit_position_study(fname, K_VALUES)
% Rows: [K, bit, npcr, uaci, psnr]. One image suffices given the
% image-independence result checked in the main pass.
    plain = rgb888_pack(imread(fname));
    rows = {};
    for K = K_VALUES
        bits = unique(round(linspace(1, K, min(K, 24))));
        key1 = secret_key(K);
        c1 = xormap_rgb888_encrypt(plain, key1);
        r = zeros(numel(bits), 5);
        for b = 1:numel(bits)
            key2 = key1;
            key2(bits(b)) = ~key2(bits(b));
            c2 = xormap_rgb888_encrypt(plain, key2);
            [npcr, uaci] = npcr_uaci(c1, c2, 2^24 - 1);
            r(b, :) = [K, bits(b), npcr, uaci, ...
                psnr_db(rgb888_unpack(c1), rgb888_unpack(c2))];
        end
        rows{end + 1} = r; %#ok<AGROW>
    end
    B = vertcat(rows{:});
end

function v = diff_checksum(diff_row)
% Order-sensitive checksum of the leading words of a difference image,
% which MUST already be in row-major (keystream) order.
    d = double(diff_row(:));
    n = min(numel(d), 4096);
    v = mod(sum(d(1:n) .* (1:n).'), 2^31);
end

% ---------------------------------------------------------------------------

function write_key_sensitivity(results_dir, R, B, names, vols, task_img, ...
                               K_VALUES, flip_bit, same_per_K)
    csv = fullfile(results_dir, 'key_sensitivity_rgb888.csv');
    fid = fopen(csv, 'w');
    fprintf(fid, ['image,volume,K,num_pixels,flipped_key_bit,npcr_key_packed,' ...
        'uaci_key_packed,psnr_cipher_pair_db,psnr_plain_wrongkey_db,' ...
        'first_differing_word,key_diff_checksum\n']);
    for r = 1:size(R, 1)
        fprintf(fid, '%s,%s,%d,%d,%d,%.6f,%.6f,%.4f,%.4f,%d,%d\n', ...
            names{task_img(r)}, vols{task_img(r)}, R(r,1), R(r,2), flip_bit, ...
            R(r,3), R(r,4), R(r,5), R(r,6), R(r,13), R(r,12));
    end
    fclose(fid);
    fprintf('Wrote %s\n', csv);

    csv_b = fullfile(results_dir, 'key_sensitivity_bits_rgb888.csv');
    fid = fopen(csv_b, 'w');
    fprintf(fid, 'K,flipped_key_bit,npcr_key_packed,uaci_key_packed,psnr_cipher_pair_db\n');
    for r = 1:size(B, 1)
        fprintf(fid, '%d,%d,%.6f,%.6f,%.4f\n', B(r,1), B(r,2), B(r,3), B(r,4), B(r,5));
    end
    fclose(fid);
    fprintf('Wrote %s\n', csv_b);

    [npcr_ideal, uaci_ideal] = npcr_uaci_ideal(24);
    fh = new_figure(13.5, 8.6);
    tl = tiledlayout(fh, 2, 2, 'TileSpacing', 'compact', 'Padding', 'compact');

    ax = nexttile(tl);
    plot_metric_vs_K(ax, R(:,1), R(:,3), K_VALUES, npcr_ideal, ...
        'NPCR, packed word (%)', 'one flipped key bit');
    ax = nexttile(tl);
    plot_metric_vs_K(ax, R(:,1), R(:,4), K_VALUES, uaci_ideal, ...
        'UACI, packed word (%)', 'one flipped key bit');
    ax = nexttile(tl);
    plot_metric_vs_K(ax, R(:,1), R(:,6), K_VALUES, NaN, ...
        'PSNR, plain vs wrong-key decryption (dB)', 'lower is better');

    ax = nexttile(tl);
    hold(ax, 'on'); style_axes(ax);
    scatter(ax, B(:,1), B(:,3), 14, [0.62 0.62 0.60], 'filled', ...
        'MarkerFaceAlpha', 0.55, 'MarkerEdgeColor', 'none', ...
        'DisplayName', 'each flipped bit position');
    km = arrayfun(@(K) mean(B(B(:,1) == K, 3)), K_VALUES);
    plot(ax, K_VALUES, km, '-o', 'Color', [0.165 0.471 0.839], 'LineWidth', 2, ...
        'MarkerSize', 5.5, 'MarkerFaceColor', [0.165 0.471 0.839], ...
        'MarkerEdgeColor', 'none', 'DisplayName', 'mean over bit positions');
    yline(ax, npcr_ideal, '--', 'ideal', 'Color', [0.35 0.35 0.35], ...
        'LineWidth', 1, 'FontSize', 10, 'HandleVisibility', 'off');
    xlim(ax, [6 402]); xticks(ax, [24 48:48:384]);
    xlabel(ax, 'K (state bits)'); ylabel(ax, 'NPCR (%)');
    title(ax, 'By flipped bit position (single image)', ...
        'FontWeight', 'normal', 'FontSize', 12.5);
    lg = legend(ax, 'Location', 'southeast'); lg.Box = 'off'; lg.FontSize = 10;

    if all(same_per_K)
        verdict = 'confirmed at every K';
    else
        verdict = sprintf('HOLDS AT ONLY %d OF %d K', sum(same_per_K), numel(K_VALUES));
    end
    layout_title(tl, {'Key sensitivity: RGB888, every SIPI colour image', ...
        sprintf(['one flipped key bit \\bullet NPCR/UACI on the packed 24-bit word ' ...
                 '\\bullet difference image-independent: %s'], verdict)});
    export_pdf(fh, fullfile(results_dir, 'key_sensitivity_rgb888.pdf'));
end

function write_histogram(results_dir, R, names, vols, task_img, K_VALUES, files)
    csv = fullfile(results_dir, 'histogram_analysis_rgb888.csv');
    fid = fopen(csv, 'w');
    fprintf(fid, ['image,volume,K,num_pixels,mean_chi2_plain,mean_chi2_cipher,' ...
        'chi2_critical_005,cipher_uniform_pass\n']);
    for r = 1:size(R, 1)
        fprintf(fid, '%s,%s,%d,%d,%.4f,%.4f,%.4f,%d\n', ...
            names{task_img(r)}, vols{task_img(r)}, R(r,1), R(r,2), ...
            R(r,7), R(r,8), R(r,9), R(r,8) <= R(r,9));
    end
    fclose(fid);
    fprintf('Wrote %s\n', csv);

    pass_rate = 100 * mean(R(:,8) <= R(:,9));
    fprintf('Cipher histograms passing chi-square at alpha=0.05: %.1f%%\n', pass_rate);

    fh = new_figure(13.5, 8.6);
    tl = tiledlayout(fh, 2, 2, 'TileSpacing', 'compact', 'Padding', 'compact');

    ax = nexttile(tl);
    plot_metric_vs_K(ax, R(:,1), R(:,8), K_VALUES, R(1,9), ...
        'Cipher \chi^2, mean over R/G/B', 'dashed = 5% critical value');

    ax = nexttile(tl);
    hold(ax, 'on'); style_axes(ax);
    histogram(ax, log10(max(R(:,7), 1)), 40, 'FaceColor', [0.922 0.408 0.204], ...
        'EdgeColor', 'none', 'FaceAlpha', 0.75, 'DisplayName', 'plain');
    histogram(ax, log10(max(R(:,8), 1)), 40, 'FaceColor', [0.165 0.471 0.839], ...
        'EdgeColor', 'none', 'FaceAlpha', 0.75, 'DisplayName', 'cipher');
    xline(ax, log10(R(1,9)), '--', '5% critical', 'Color', [0.35 0.35 0.35], ...
        'LineWidth', 1, 'FontSize', 10, 'HandleVisibility', 'off');
    xlabel(ax, 'log_{10} \chi^2'); ylabel(ax, 'count');
    title(ax, 'Plain vs cipher \chi^2', 'FontWeight', 'normal', 'FontSize', 12.5);
    lg = legend(ax, 'Location', 'north'); lg.Box = 'off'; lg.FontSize = 10;

    plain = rgb888_pack(imread(files{1}));
    K = K_VALUES(end);
    cipher = xormap_rgb888_encrypt(plain, secret_key(K));

    ax = nexttile(tl);
    draw_rgb_histogram(ax, rgb888_unpack(plain), ...
        sprintf('Plain histogram, R/G/B (%s)', short_name(files{1})));
    ax = nexttile(tl);
    draw_rgb_histogram(ax, rgb888_unpack(cipher), ...
        sprintf('Cipher histogram, R/G/B (K=%d)', K));

    layout_title(tl, {'Histogram analysis: RGB888, every SIPI colour image', ...
        sprintf(['\\chi^2 uniformity per 8-bit channel \\bullet cipher passes at ' ...
                 '\\alpha=0.05 in %.1f%% of (image, K) cases'], pass_rate)});
    export_pdf(fh, fullfile(results_dir, 'histogram_analysis_rgb888.pdf'));
end

function write_psnr(results_dir, R, names, vols, task_img, K_VALUES)
    csv = fullfile(results_dir, 'psnr_analysis_rgb888.csv');
    fid = fopen(csv, 'w');
    fprintf(fid, ['image,volume,K,num_pixels,psnr_plain_cipher_db,' ...
        'psnr_plain_wrongkey_db,psnr_roundtrip_db\n']);
    for r = 1:size(R, 1)
        fprintf(fid, '%s,%s,%d,%d,%.4f,%.4f,%s\n', ...
            names{task_img(r)}, vols{task_img(r)}, R(r,1), R(r,2), ...
            R(r,10), R(r,6), fmt_inf(R(r,11)));
    end
    fclose(fid);
    fprintf('Wrote %s\n', csv);

    n_inf = sum(isinf(R(:,11)));
    fprintf('Round-trip PSNR is +Inf (exact recovery) in %d of %d cases.\n', ...
        n_inf, size(R, 1));

    fh = new_figure(13.5, 5.4);
    tl = tiledlayout(fh, 1, 2, 'TileSpacing', 'compact', 'Padding', 'compact');
    ax = nexttile(tl);
    plot_metric_vs_K(ax, R(:,1), R(:,10), K_VALUES, NaN, ...
        'PSNR, plain vs cipher (dB)', 'lower is better');
    ax = nexttile(tl);
    plot_metric_vs_K(ax, R(:,1), R(:,6), K_VALUES, NaN, ...
        'PSNR, plain vs wrong-key decryption (dB)', 'lower is better');

    layout_title(tl, {'PSNR: RGB888, every SIPI colour image', ...
        sprintf(['peak 255 over R/G/B \\bullet round-trip PSNR is +Inf in %d/%d ' ...
                 'cases (XOR is lossless)'], n_inf, size(R,1))});
    export_pdf(fh, fullfile(results_dir, 'psnr_analysis_rgb888.pdf'));
end

% --- shared plotting -------------------------------------------------------

function plot_metric_vs_K(ax, k, v, K_VALUES, ideal, ylab, note)
    hold(ax, 'on'); style_axes(ax);
    finite = isfinite(v);
    scatter(ax, k(finite), v(finite), 9, [0.62 0.62 0.60], 'filled', ...
        'MarkerFaceAlpha', 0.25, 'MarkerEdgeColor', 'none');
    m = arrayfun(@(K) mean(v(k == K & finite)), K_VALUES);
    plot(ax, K_VALUES, m, '-o', 'Color', [0.165 0.471 0.839], 'LineWidth', 2, ...
        'MarkerSize', 5.5, 'MarkerFaceColor', [0.165 0.471 0.839], 'MarkerEdgeColor', 'none');
    if ~isnan(ideal)
        yline(ax, ideal, '--', 'ideal', 'Color', [0.35 0.35 0.35], ...
            'LineWidth', 1, 'FontSize', 10, 'LabelHorizontalAlignment', 'right');
    end
    xlim(ax, [6 402]); xticks(ax, [24 48:48:384]);
    xlabel(ax, 'K (state bits)'); ylabel(ax, ylab);
    title(ax, note, 'FontWeight', 'normal', 'FontSize', 12.5);
end

function draw_rgb_histogram(ax, rgb, ttl)
    hold(ax, 'on'); style_axes(ax);
    cols = [0.85 0.20 0.20; 0.20 0.60 0.25; 0.20 0.35 0.85];
    labels = {'R', 'G', 'B'};
    for ch = 1:3
        counts = histcounts(double(reshape(rgb(:, :, ch), 1, [])), -0.5:1:255.5);
        plot(ax, 0:255, counts, 'Color', [cols(ch, :) 0.85], 'LineWidth', 1.1, ...
            'DisplayName', labels{ch});
    end
    yline(ax, numel(rgb) / 3 / 256, '--', 'flat', 'Color', [0.35 0.35 0.35], ...
        'LineWidth', 1, 'FontSize', 10, 'HandleVisibility', 'off');
    xlim(ax, [-2 257]);
    xlabel(ax, 'level'); ylabel(ax, 'count');
    title(ax, ttl, 'FontWeight', 'normal', 'FontSize', 12.5);
    lg = legend(ax, 'Location', 'best'); lg.Box = 'off'; lg.FontSize = 10;
end

function s = short_name(path)
    [~, s] = fileparts(path);
    s = strrep(s, '_', '\_');
end

function s = fmt_inf(v)
    if isinf(v)
        s = 'Inf';
    else
        s = sprintf('%.4f', v);
    end
end

function start_pool()
    n = feature('numcores');
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

function fh = new_figure(w, h)
    fh = figure('Visible', 'off', 'Units', 'inches', 'Position', [1 1 w h], 'Color', 'w');
    try %#ok<TRYNC>
        theme(fh, 'light');
    end
    set(fh, 'Color', 'w');
end

function style_axes(ax)
    ax.FontSize = 11.5; ax.LineWidth = 0.9; ax.Box = 'off'; ax.TickDir = 'out';
    ax.XColor = [0.043 0.043 0.043]; ax.YColor = [0.043 0.043 0.043];
    ax.GridAlpha = 0.12; grid(ax, 'on');
end

function layout_title(tl, lines)
    title(tl, lines{1}, 'FontSize', 15, 'FontWeight', 'bold', 'Color', [0.043 0.043 0.043]);
    if numel(lines) > 1
        subtitle(tl, lines{2}, 'FontSize', 11, 'Color', [0.32 0.32 0.32]);
    end
end

function export_pdf(fh, path)
% Vector PDF only: this repo produces no raster output.
    exportgraphics(fh, path, 'ContentType', 'vector', 'BackgroundColor', 'white');
    close(fh);
    fprintf('Wrote %s\n', path);
end

function [files, names, vols] = read_manifest(manifest_path, img_dir)
    fid = fopen(manifest_path);
    header = strsplit(fgetl(fid), ',');
    fcol = find(strcmp(header, 'filename'));
    ncol = find(strcmp(header, 'name'));
    vcol = find(strcmp(header, 'volume'));
    files = {}; names = {}; vols = {};
    while ~feof(fid)
        line = fgetl(fid);
        if isempty(line)
            continue;
        end
        parts = strsplit(line, ',');
        files{end + 1} = fullfile(img_dir, parts{fcol}); %#ok<AGROW>
        names{end + 1} = parts{ncol}; %#ok<AGROW>
        vols{end + 1} = parts{vcol}; %#ok<AGROW>
    end
    fclose(fid);
end
