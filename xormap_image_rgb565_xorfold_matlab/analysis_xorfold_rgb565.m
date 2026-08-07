function analysis_xorfold_rgb565()
%ANALYSIS_XORFOLD_RGB565 Key sensitivity, histogram and PSNR analysis, RGB565 XOR-fold.
%   The XOR-fold counterpart of ../xormap_image_matlab/analysis_gray.m and
%   ../xormap_image_rgb888_matlab/analysis_rgb888.m: the same three tests,
%   run against xormap_rgb565_xorfold_encrypt/decrypt (one folded K-bit
%   state per pixel, see README.md) instead of the K/16-pixels-per-
%   iteration cipher. K = 32:32:512, the same grid as sweep_k_xorfold_rgb565.m.
%   Writes one CSV and one vector PDF per test into results/:
%
%     key_sensitivity_xorfold_rgb565.{csv,pdf}  NPCR/UACI/PSNR between two
%                                      ciphers whose keys differ in ONE
%                                      bit, plus the PSNR of a wrong-key
%                                      decryption, plus a per-bit study.
%     histogram_analysis_xorfold_rgb565.{csv,pdf}  chi-square uniformity
%                                      of the plain and cipher R/G/B
%                                      histograms.
%     psnr_analysis_xorfold_rgb565.{csv,pdf}  PSNR(plain, cipher) and the
%                                      round-trip PSNR.
%
%   All three share the same two encryptions per (image, K), computed in
%   ONE pass rather than three.
%
%   WHY CHI-SQUARE IS REPORTED AS A RATIO, NOT A RAW STATISTIC. RGB565
%   channels are NOT uniform bit depth (R=5, G=6, B=5 bits), unlike
%   grayscale/RGB888's flat 8 bits everywhere, so each channel has its own
%   degrees of freedom and its own chi-square critical value (dof=31 for a
%   5-bit channel, dof=63 for 6-bit). A raw chi2 average across channels of
%   different dof is not meaningful. Instead each channel's statistic is
%   normalised by ITS OWN critical value (chi2/critical), and those three
%   ratios are averaged: a ratio <= 1 means "does not reject uniformity at
%   alpha=0.05" regardless of which channel it came from.
%
%   WHY PSNR IS ALSO A PER-CHANNEL MEAN. For the same reason -- R and B
%   saturate at 31, G at 63, not the 8-bit-everywhere 255 that PSNR_DB
%   defaults to -- PSNR is computed per channel against that channel's own
%   peak value (2^nbits - 1) and averaged, mirroring the normalised-entropy
%   convention already used by sweep_k_xorfold_rgb565.m.
%
%   IS PSNR APPLICABLE HERE? As in the sibling analyses: PSNR(plain,
%   cipher) and PSNR(plain, wrong-key decryption) are meaningful and LOW is
%   good (~8-9 dB per channel). The round-trip PSNR is exactly +Inf by
%   construction (lossless XOR cipher, MSE identically zero) -- asserted as
%   a correctness check, not a tuning signal.
%
%   KEY SENSITIVITY OF A LINEAR MAP. xormap_transform is linear over GF(2)
%   and the fold is itself an XOR (also linear), so with
%   S1 = key1 XOR h(P), S2 = key2 XOR h(P):
%
%       C1 XOR C2 = fold(KS(S1)) XOR fold(KS(S2)) = fold(KS(S1) XOR KS(S2))
%
%   which — because both xormap_transform and the fold are GF(2)-linear —
%   again depends only on key1 XOR key2, not on the plaintext. Tested
%   directly below (diff_checksum must be identical across images at a
%   given K) rather than assumed.

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
            error('analysis_xorfold_rgb565:noImage', ...
                'Image not found: %s. Run xormap_image_rgb565_matlab/download_images.m first.', fname);
        end
    end

    K_VALUES = 32:32:512;
    FLIP_BIT = 1;
    NBITS = [5, 6, 5];

    num_images = numel(images);
    [II, KK] = ndgrid(1:num_images, K_VALUES);
    task_img = II(:);
    task_K = KK(:);
    num_tasks = numel(task_img);

    fprintf('%d images x %d K values = %d tasks\n', num_images, numel(K_VALUES), num_tasks);

    % columns: K, num_pixels, npcr_key, uaci_key, psnr_c1c2,
    %          psnr_plain_wrongkey, chi2ratio_plain, chi2ratio_cipher,
    %          psnr_plain_cipher, psnr_roundtrip, diff_checksum,
    %          first_differing_word
    R = zeros(num_tasks, 12);

    start_pool();
    t_all = tic;
    parfor idx = 1:num_tasks
        img = images(task_img(idx));
        K = task_K(idx);

        rgb = imread(fullfile(rgb565_dir, 'images', img.file));
        plain = rgb888_to_rgb565(rgb);
        num_pixels = numel(plain);

        key1 = secret_key(K);
        key2 = key1;
        key2(FLIP_BIT) = ~key2(FLIP_BIT);

        [c1, seed1] = xormap_rgb565_xorfold_encrypt(plain, key1);
        c2          = xormap_rgb565_xorfold_encrypt(plain, key2);

        cipher_diff = bitxor(c1, c2);

        % Row-major, matching the pixel-major keystream layout used by
        % the encrypt/decrypt functions (they reshape via .' too).
        diff_row = reshape(cipher_diff.', 1, []);
        first_diff = find(diff_row ~= 0, 1, 'first');
        if isempty(first_diff)
            first_diff = 0;
        end

        % Wrong-key decryption, free from XOR algebra:
        %   decrypt(C1, S2) = P XOR (C1 XOR C2)
        wrong = bitxor(plain, cipher_diff);
        if idx == 1
            [~, seed2] = xormap_rgb565_xorfold_encrypt(plain, key2);
            assert(isequal(xormap_rgb565_xorfold_decrypt(c1, seed2), wrong), ...
                'wrong-key shortcut disagrees with decrypt()');
        end

        recovered = xormap_rgb565_xorfold_decrypt(c1, seed1);
        assert(isequal(recovered, plain), 'round trip failed for %s K=%d', img.file, K);

        [npcr_key, uaci_key] = npcr_uaci(c1, c2, 65535);

        R(idx, :) = [K, num_pixels, npcr_key, uaci_key, ...
            mean_psnr_channels(c1, c2, NBITS), ...
            mean_psnr_channels(plain, wrong, NBITS), ...
            mean_chi2_ratio(plain, NBITS), mean_chi2_ratio(c1, NBITS), ...
            mean_psnr_channels(plain, c1, NBITS), ...
            mean_psnr_channels(plain, recovered, NBITS), ...
            diff_checksum(diff_row), first_diff];

        if mod(idx, 8) == 0
            fprintf('  %d/%d tasks done (%.1fs elapsed)\n', idx, num_tasks, toc(t_all));
        end
    end
    fprintf('Main pass finished in %.1f s.\n', toc(t_all));

    % --- the linearity prediction: C1 XOR C2 must not depend on the image
    same_per_K = arrayfun(@(K) numel(unique(R(R(:,1) == K, 11))) == 1, K_VALUES);
    fprintf(['Key-difference independent of image: %d of %d K values ' ...
             '(prediction: all %d).\n'], sum(same_per_K), numel(K_VALUES), numel(K_VALUES));

    % --- per-bit-position study (one image suffices if the above holds)
    B = bit_position_study(images(1), rgb565_dir, K_VALUES, NBITS);

    write_key_sensitivity(results_dir, R, B, images, task_img, K_VALUES, ...
        FLIP_BIT, same_per_K);
    write_histogram(results_dir, R, images, task_img, K_VALUES, rgb565_dir, NBITS);
    write_psnr(results_dir, R, images, task_img, K_VALUES);
end

% ---------------------------------------------------------------------------

function B = bit_position_study(img, rgb565_dir, K_VALUES, NBITS)
% Flip each of a spread of key-bit positions and measure the resulting
% ciphertext difference. Rows: [K, bit, npcr, uaci, psnr].
%
% Sampled at up to 12 positions per K, not the 24 used by the non-fold
% siblings: every xormap_rgb565_xorfold_encrypt call performs N state
% transitions REGARDLESS of K (that is the whole point of the fold
% variant), so this loop's cost is dominated purely by the number of
% samples, not by how cheap a single encryption is.
    rgb = imread(fullfile(rgb565_dir, 'images', img.file));
    plain = rgb888_to_rgb565(rgb);
    rows = cell(1, numel(K_VALUES));
    parfor ki = 1:numel(K_VALUES)
        K = K_VALUES(ki);
        bits = unique(round(linspace(1, K, min(K, 12))));
        key1 = secret_key(K);
        c1 = xormap_rgb565_xorfold_encrypt(plain, key1);
        r = zeros(numel(bits), 5);
        for b = 1:numel(bits)
            key2 = key1;
            key2(bits(b)) = ~key2(bits(b));
            c2 = xormap_rgb565_xorfold_encrypt(plain, key2);
            [npcr, uaci] = npcr_uaci(c1, c2, 65535);
            r(b, :) = [K, bits(b), npcr, uaci, mean_psnr_channels(c1, c2, NBITS)];
        end
        rows{ki} = r;
    end
    B = vertcat(rows{:});
end

function v = diff_checksum(diff_row)
% Cheap order-sensitive checksum of the leading words of a difference
% image, already in row-major (pixel/keystream) order.
    d = double(diff_row(:));
    n = min(numel(d), 4096);
    v = mod(sum(d(1:n) .* (1:n).'), 2^31);
end

function ratio = mean_chi2_ratio(rgb565_img, NBITS)
% Mean of chi2/critical across R/G/B — see the file-level docstring for
% why a ratio, not a raw chi2, is the right thing to average here.
    [Rc, Gc, Bc] = rgb565_to_channels(rgb565_img);
    chans = {Rc, Gc, Bc};
    r = zeros(1, 3);
    for c = 1:3
        [chi2, ~, crit] = chi_square_uniformity(chans{c}, NBITS(c));
        r(c) = chi2 / crit;
    end
    ratio = mean(r);
end

function p = mean_psnr_channels(a565, b565, NBITS)
% Mean of per-channel PSNR, each against its own channel's peak value
% (2^nbits - 1) — see the file-level docstring.
    [Ra, Ga, Ba] = rgb565_to_channels(a565);
    [Rb, Gb, Bb] = rgb565_to_channels(b565);
    a = {Ra, Ga, Ba};
    b = {Rb, Gb, Bb};
    p = zeros(1, 3);
    for c = 1:3
        p(c) = psnr_db(a{c}, b{c}, 2 ^ NBITS(c) - 1);
    end
    p = mean(p);
end

% ---------------------------------------------------------------------------

function write_key_sensitivity(results_dir, R, B, images, task_img, ...
                               K_VALUES, flip_bit, same_per_K)
    csv = fullfile(results_dir, 'key_sensitivity_xorfold_rgb565.csv');
    fid = fopen(csv, 'w');
    fprintf(fid, ['image,K,num_pixels,flipped_key_bit,npcr_key_packed,' ...
        'uaci_key_packed,psnr_cipher_pair_db,psnr_plain_wrongkey_db,' ...
        'first_differing_word,key_diff_checksum\n']);
    for r = 1:size(R, 1)
        fprintf(fid, '%s,%d,%d,%d,%.6f,%.6f,%.4f,%.4f,%d,%d\n', ...
            images(task_img(r)).name, R(r,1), R(r,2), flip_bit, ...
            R(r,3), R(r,4), R(r,5), R(r,6), R(r,12), R(r,11));
    end
    fclose(fid);
    fprintf('Wrote %s\n', csv);

    csv_b = fullfile(results_dir, 'key_sensitivity_bits_xorfold_rgb565.csv');
    fid = fopen(csv_b, 'w');
    fprintf(fid, 'K,flipped_key_bit,npcr_key_packed,uaci_key_packed,psnr_cipher_pair_db\n');
    for r = 1:size(B, 1)
        fprintf(fid, '%d,%d,%.6f,%.6f,%.4f\n', B(r,1), B(r,2), B(r,3), B(r,4), B(r,5));
    end
    fclose(fid);
    fprintf('Wrote %s\n', csv_b);

    [npcr_ideal, uaci_ideal] = npcr_uaci_ideal(16);
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
    xlim(ax, [16 528]); xticks(ax, 32:64:512);
    xlabel(ax, 'K (state bits)'); ylabel(ax, 'NPCR (%)');
    title(ax, 'By flipped bit position (single image)', ...
        'FontWeight', 'normal', 'FontSize', 12.5);
    lg = legend(ax, 'Location', 'southeast'); lg.Box = 'off'; lg.FontSize = 10;

    if all(same_per_K)
        verdict = 'confirmed at every K';
    else
        verdict = sprintf('HOLDS AT ONLY %d OF %d K', sum(same_per_K), numel(K_VALUES));
    end
    layout_title(tl, {'Key sensitivity: RGB565 XOR-fold, one folded state per pixel', ...
        sprintf(['one flipped key bit \\bullet K = 32:32:512 \\bullet ' ...
                 'ciphertext difference image-independent: %s'], verdict)});
    export_pdf(fh, fullfile(results_dir, 'key_sensitivity_xorfold_rgb565.pdf'));
end

function write_histogram(results_dir, R, images, task_img, K_VALUES, rgb565_dir, NBITS)
    csv = fullfile(results_dir, 'histogram_analysis_xorfold_rgb565.csv');
    fid = fopen(csv, 'w');
    fprintf(fid, ['image,K,num_pixels,mean_chi2ratio_plain,mean_chi2ratio_cipher,' ...
        'cipher_uniform_pass\n']);
    for r = 1:size(R, 1)
        fprintf(fid, '%s,%d,%d,%.4f,%.4f,%d\n', ...
            images(task_img(r)).name, R(r,1), R(r,2), R(r,7), R(r,8), R(r,8) <= 1);
    end
    fclose(fid);
    fprintf('Wrote %s\n', csv);

    pass_rate = 100 * mean(R(:,8) <= 1);
    fprintf('Cipher histograms passing chi-square at alpha=0.05: %.1f%%\n', pass_rate);

    fh = new_figure(13.5, 8.6);
    tl = tiledlayout(fh, 2, 2, 'TileSpacing', 'compact', 'Padding', 'compact');

    ax = nexttile(tl);
    plot_metric_vs_K(ax, R(:,1), R(:,8), K_VALUES, 1.0, ...
        'Cipher \chi^2 / critical, mean over R/G/B', 'dashed = 5% critical (ratio 1.0)');

    ax = nexttile(tl);
    hold(ax, 'on'); style_axes(ax);
    histogram(ax, log10(max(R(:,7), 1e-3)), 40, 'FaceColor', [0.922 0.408 0.204], ...
        'EdgeColor', 'none', 'FaceAlpha', 0.75, 'DisplayName', 'plain');
    histogram(ax, log10(max(R(:,8), 1e-3)), 40, 'FaceColor', [0.165 0.471 0.839], ...
        'EdgeColor', 'none', 'FaceAlpha', 0.75, 'DisplayName', 'cipher');
    xline(ax, 0, '--', '5% critical', 'Color', [0.35 0.35 0.35], ...
        'LineWidth', 1, 'FontSize', 10, 'HandleVisibility', 'off');
    xlabel(ax, 'log_{10}(\chi^2 / critical)'); ylabel(ax, 'count');
    title(ax, 'Plain vs cipher \chi^2 ratio', 'FontWeight', 'normal', 'FontSize', 12.5);
    lg = legend(ax, 'Location', 'north'); lg.Box = 'off'; lg.FontSize = 10;

    rgb = imread(fullfile(rgb565_dir, 'images', images(1).file));
    plain = rgb888_to_rgb565(rgb);
    K = K_VALUES(end);
    cipher = xormap_rgb565_xorfold_encrypt(plain, secret_key(K));

    ax = nexttile(tl);
    draw_rgb565_histogram(ax, plain, NBITS, ...
        sprintf('Plain histogram, R/G/B (%s)', short_name(images(1).name)));
    ax = nexttile(tl);
    draw_rgb565_histogram(ax, cipher, NBITS, ...
        sprintf('Cipher histogram, R/G/B (K=%d)', K));

    layout_title(tl, {'Histogram analysis: RGB565 XOR-fold', ...
        sprintf(['\\chi^2 uniformity per channel (own bit depth: R/B=5, G=6) \\bullet ' ...
                 'cipher passes at \\alpha=0.05 in %.1f%% of (image, K) cases'], pass_rate)});
    export_pdf(fh, fullfile(results_dir, 'histogram_analysis_xorfold_rgb565.pdf'));
end

function write_psnr(results_dir, R, images, task_img, K_VALUES)
    csv = fullfile(results_dir, 'psnr_analysis_xorfold_rgb565.csv');
    fid = fopen(csv, 'w');
    fprintf(fid, ['image,K,num_pixels,psnr_plain_cipher_db,' ...
        'psnr_plain_wrongkey_db,psnr_roundtrip_db\n']);
    for r = 1:size(R, 1)
        fprintf(fid, '%s,%d,%d,%.4f,%.4f,%s\n', ...
            images(task_img(r)).name, R(r,1), R(r,2), R(r,9), R(r,6), fmt_inf(R(r,10)));
    end
    fclose(fid);
    fprintf('Wrote %s\n', csv);

    n_inf = sum(isinf(R(:,10)));
    fprintf('Round-trip PSNR is +Inf (exact recovery) in %d of %d cases.\n', ...
        n_inf, size(R, 1));

    fh = new_figure(13.5, 5.4);
    tl = tiledlayout(fh, 1, 2, 'TileSpacing', 'compact', 'Padding', 'compact');

    ax = nexttile(tl);
    plot_metric_vs_K(ax, R(:,1), R(:,9), K_VALUES, NaN, ...
        'PSNR, plain vs cipher (dB)', 'lower is better');
    ax = nexttile(tl);
    plot_metric_vs_K(ax, R(:,1), R(:,6), K_VALUES, NaN, ...
        'PSNR, plain vs wrong-key decryption (dB)', 'lower is better');

    layout_title(tl, {'PSNR: RGB565 XOR-fold', ...
        sprintf(['per-channel peaks (31/63/31) \\bullet round-trip PSNR is ' ...
                 '+Inf in %d/%d cases (XOR is lossless)'], n_inf, size(R,1))});
    export_pdf(fh, fullfile(results_dir, 'psnr_analysis_xorfold_rgb565.pdf'));
end

% --- shared plotting -------------------------------------------------------

function plot_metric_vs_K(ax, k, v, K_VALUES, ideal, ylab, note)
    hold(ax, 'on'); style_axes(ax);
    finite = isfinite(v);
    scatter(ax, k(finite), v(finite), 12, [0.62 0.62 0.60], 'filled', ...
        'MarkerFaceAlpha', 0.35, 'MarkerEdgeColor', 'none');
    m = arrayfun(@(K) mean(v(k == K & finite)), K_VALUES);
    plot(ax, K_VALUES, m, '-o', 'Color', [0.165 0.471 0.839], 'LineWidth', 2, ...
        'MarkerSize', 5.5, 'MarkerFaceColor', [0.165 0.471 0.839], 'MarkerEdgeColor', 'none');
    if ~isnan(ideal)
        yline(ax, ideal, '--', 'ideal', 'Color', [0.35 0.35 0.35], ...
            'LineWidth', 1, 'FontSize', 10, 'LabelHorizontalAlignment', 'right');
    end
    xlim(ax, [16 528]); xticks(ax, 32:64:512);
    xlabel(ax, 'K (state bits)'); ylabel(ax, ylab);
    title(ax, note, 'FontWeight', 'normal', 'FontSize', 12.5);
end

function draw_rgb565_histogram(ax, rgb565_img, NBITS, ttl)
    hold(ax, 'on'); style_axes(ax);
    [Rc, Gc, Bc] = rgb565_to_channels(rgb565_img);
    chans = {Rc, Gc, Bc};
    cols = [0.85 0.20 0.20; 0.20 0.60 0.25; 0.20 0.35 0.85];
    labels = {'R (5b)', 'G (6b)', 'B (5b)'};
    for c = 1:3
        levels = 2 ^ NBITS(c);
        counts = histcounts(double(chans{c}(:)), -0.5:1:(levels - 0.5));
        plot(ax, 0:(levels - 1), counts, 'Color', [cols(c, :) 0.85], 'LineWidth', 1.1, ...
            'DisplayName', labels{c});
    end
    xlim(ax, [-2 66]);
    xlabel(ax, 'level'); ylabel(ax, 'count');
    title(ax, ttl, 'FontWeight', 'normal', 'FontSize', 12.5);
    lg = legend(ax, 'Location', 'best'); lg.Box = 'off'; lg.FontSize = 10;
end

function s = short_name(name)
    s = strrep(name, '_', '\_');
end

function s = fmt_inf(v)
    if isinf(v)
        s = 'Inf';
    else
        s = sprintf('%.4f', v);
    end
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
% Vector PDF only, matching every other results/ figure in this repo.
    exportgraphics(fh, path, 'ContentType', 'vector', 'BackgroundColor', 'white');
    close(fh);
    fprintf('Wrote %s\n', path);
end

function start_pool()
% Size the pool to the machine's logical core count (nproc), not just
% MATLAB's feature('numcores') (physical cores only).
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
