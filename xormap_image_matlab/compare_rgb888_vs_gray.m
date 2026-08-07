function compare_rgb888_vs_gray()
%COMPARE_RGB888_VS_GRAY High-resolution side-by-side RGB888 vs grayscale.
%   Plots the RGB888 sweep (../xormap_image_rgb888_matlab, 51 SIPI colour
%   images) against the all-grayscale sweep (this folder, 159 SIPI
%   grayscale images) over the shared grid K = 24:24:384.
%
%   RGB888 on the left, grayscale on the right, with IDENTICAL x and y
%   limits on both panels so panel-to-panel differences are real. Faint
%   dots are the individual images; the bold line is the mean over them.
%
%   COMPARABILITY, read before drawing conclusions from the NPCR/UACI
%   figure. Entropy and correlation are directly comparable: both are
%   measured per 8-bit channel (RGB888 averages its three channels,
%   grayscale has one), so both share the ideal 8.0 bits and 0.0
%   correlation. NPCR and UACI are NOT measured on the same alphabet --
%   RGB888 evaluates them on the packed 24-bit word (L=2^24) and grayscale
%   on 8-bit pixels (L=2^8), and both ideals depend on L:
%
%       NPCR_ideal = 100*(L-1)/L        UACI_ideal = 100*(L+1)/(3L)
%
%   so the targets differ (99.99999% / 33.3333% vs 99.6094% / 33.4635%).
%   Each panel therefore carries its OWN ideal line, and the shared y-axis
%   exists to show where each sits relative to its own target -- not to
%   suggest the two numbers are the same measurement.
%
%   Writes vector PDF plus 300-dpi PNG into results/comparison/.

    this_dir = fileparts(mfilename('fullpath'));
    addpath(this_dir);   % npcr_uaci_ideal

    % read_sweep.m owns every detail of loading these CSVs: the differing
    % column names between the two sweeps, the datetime mis-parse of the
    % image column, and each run's NPCR/UACI ideal.
    [gray, gray_meta] = read_sweep('gray');
    [rgb,  rgb_meta]  = read_sweep('rgb888');

    K_VALUES = (24:24:384).';
    assert(isequal(unique(gray.K), K_VALUES) && isequal(unique(rgb.K), K_VALUES), ...
        'both sweeps must cover K = 24:24:384');

    n_rgb  = rgb_meta.num_images;
    n_gray = gray_meta.num_images;

    out_dir = fullfile(this_dir, 'results', 'comparison');
    if ~exist(out_dir, 'dir')
        mkdir(out_dir);
    end

    % Validated categorical slots 1 and 2 (all-pairs CVD dE 24.7).
    BLUE   = [0.165 0.471 0.839];   % #2a78d6
    ORANGE = [0.922 0.408 0.204];   % #eb6834
    GRAY   = [0.62 0.62 0.60];
    INK    = [0.043 0.043 0.043];

    npcr_ideal24 = rgb_meta.npcr_ideal;   uaci_ideal24 = rgb_meta.uaci_ideal;
    npcr_ideal8  = gray_meta.npcr_ideal;  uaci_ideal8  = gray_meta.uaci_ideal;

    left_label  = sprintf('RGB888: %d SIPI colour images', n_rgb);
    right_label = sprintf('Grayscale: %d SIPI grayscale images', n_gray);

    % --- entropy ---------------------------------------------------------
    make_single_metric_figure(K_VALUES, ...
        rgb.K,  rgb.entropy,  left_label, 8.0, ...
        gray.K, gray.entropy, right_label, 8.0, ...
        'Cipher entropy (bits per 8-bit channel)', 'ideal = 8', ...
        {'Cipher entropy: RGB888 vs grayscale', ...
         'identical axis limits on both panels \bullet K = 24:24:384 \bullet ideal 8 bits/channel'}, ...
        fullfile(out_dir, 'compare_rgb_gray_entropy'), BLUE, ORANGE, GRAY, INK);

    % --- correlation -----------------------------------------------------
    make_single_metric_figure(K_VALUES, ...
        rgb.K,  rgb.corrH, left_label, 0.0, ...
        gray.K, gray.corrH, right_label, 0.0, ...
        'Mean |horizontal correlation| of cipher', 'ideal = 0', ...
        {'Adjacent-pixel correlation: RGB888 vs grayscale', ...
         'identical axis limits on both panels \bullet K = 24:24:384 \bullet ideal 0'}, ...
        fullfile(out_dir, 'compare_rgb_gray_correlation'), BLUE, ORANGE, GRAY, INK);

    % --- NPCR / UACI -----------------------------------------------------
    make_npcr_uaci_figure(K_VALUES, ...
        rgb,  left_label,  npcr_ideal24, uaci_ideal24, ...
        gray, right_label, npcr_ideal8,  uaci_ideal8, ...
        fullfile(out_dir, 'compare_rgb_gray_npcr_uaci'), BLUE, ORANGE, INK);

    fprintf('Wrote comparison figures to %s\n', out_dir);
end

% ---------------------------------------------------------------------------

function make_single_metric_figure(K_VALUES, k1, v1, t1, ideal1, ...
                                   k2, v2, t2, ideal2, ylab, ideal_label, ...
                                   super_title, out_stem, c1, c2, gray, ink)

    ylims = shared_limits([v1; v2], [ideal1; ideal2]);

    fh = new_figure(13.5, 5.4);
    tl = tiledlayout(fh, 1, 2, 'TileSpacing', 'compact', 'Padding', 'compact');

    panels = {k1, v1, t1, ideal1, c1; ...
              k2, v2, t2, ideal2, c2};

    for p = 1:2
        ax = nexttile(tl);
        hold(ax, 'on');
        style_axes(ax, ink);

        yline(ax, panels{p, 4}, '--', ideal_label, 'Color', [0.35 0.35 0.35], ...
            'LineWidth', 1.0, 'FontSize', 11, 'LabelHorizontalAlignment', 'right', ...
            'LabelVerticalAlignment', 'bottom', 'HandleVisibility', 'off');

        scatter(ax, panels{p, 1}, panels{p, 2}, 11, gray, 'filled', ...
            'MarkerFaceAlpha', 0.30, 'MarkerEdgeColor', 'none', ...
            'DisplayName', 'individual images');

        m = group_means(panels{p, 1}, panels{p, 2}, K_VALUES);
        plot(ax, K_VALUES, m, '-o', 'Color', panels{p, 5}, 'LineWidth', 2.0, ...
            'MarkerSize', 5.5, 'MarkerFaceColor', panels{p, 5}, ...
            'MarkerEdgeColor', 'none', 'DisplayName', 'mean over all images');

        xlim(ax, [6 402]);
        ylim(ax, ylims);
        xticks(ax, [24 48:48:384]);
        xlabel(ax, 'K (state bits)');
        if p == 1
            ylabel(ax, ylab);
            lg = legend(ax, 'Orientation', 'horizontal');
            lg.FontSize = 11;
            lg.Box = 'off';
            lg.Layout.Tile = 'south';
        else
            yticklabels(ax, []);
        end
        title(ax, panels{p, 3}, 'FontWeight', 'normal', 'FontSize', 12.5);
    end

    layout_title(tl, super_title, ink);
    export_figure(fh, out_stem);
end

% ---------------------------------------------------------------------------

function make_npcr_uaci_figure(K_VALUES, T1, t1, npcr_i1, uaci_i1, ...
                               T2, t2, npcr_i2, uaci_i2, out_stem, ...
                               blue, orange, ink)
% NPCR on the left axis, UACI on the right, identical limits both panels.
% Each panel draws its OWN ideal lines: RGB888's are for L=2^24 and
% grayscale's for L=2^8, so a single shared ideal would be wrong for one
% of them (see the comparability note at the top of this file).

    npcr1 = T1.npcr;  uaci1 = T1.uaci;   % both tables carry the same
    npcr2 = T2.npcr;  uaci2 = T2.uaci;   % schema, courtesy of read_sweep

    npcr_lims = shared_limits([npcr1; npcr2], [npcr_i1; npcr_i2]);
    uaci_lims = shared_limits([uaci1; uaci2], [uaci_i1; uaci_i2]);

    fh = new_figure(14.5, 5.8);
    tl = tiledlayout(fh, 1, 2, 'TileSpacing', 'compact', 'Padding', 'compact');

    panels = {T1.K, npcr1, uaci1, t1, npcr_i1, uaci_i1; ...
              T2.K, npcr2, uaci2, t2, npcr_i2, uaci_i2};

    for p = 1:2
        K = panels{p, 1};
        ax = nexttile(tl);
        style_axes(ax, ink);

        % --- left axis: NPCR ---
        yyaxis(ax, 'left');
        hold(ax, 'on');
        ax.YAxis(1).Color = blue;
        scatter(ax, K, panels{p, 2}, 11, tint(blue), 'filled', ...
            'MarkerFaceAlpha', 0.45, 'MarkerEdgeColor', 'none', ...
            'HandleVisibility', 'off');
        plot(ax, K_VALUES, group_means(K, panels{p, 2}, K_VALUES), '-o', ...
            'Color', blue, 'LineWidth', 2.0, 'MarkerSize', 5.5, ...
            'MarkerFaceColor', blue, 'MarkerEdgeColor', 'none', ...
            'DisplayName', 'NPCR (left axis)');
        yline(ax, panels{p, 5}, '--', sprintf('ideal %.5f', panels{p, 5}), ...
            'Color', blue, 'LineWidth', 1.0, 'Alpha', 0.55, 'FontSize', 10, ...
            'LabelHorizontalAlignment', 'right', 'LabelVerticalAlignment', 'top', ...
            'HandleVisibility', 'off');
        ylim(ax, npcr_lims);
        if p == 1
            ylabel(ax, 'NPCR (%)');
        else
            yticklabels(ax, []);
        end

        % --- right axis: UACI ---
        yyaxis(ax, 'right');
        hold(ax, 'on');
        ax.YAxis(2).Color = orange;
        scatter(ax, K, panels{p, 3}, 11, tint(orange), 'filled', ...
            'MarkerFaceAlpha', 0.45, 'MarkerEdgeColor', 'none', ...
            'HandleVisibility', 'off');
        plot(ax, K_VALUES, group_means(K, panels{p, 3}, K_VALUES), '-s', ...
            'Color', orange, 'LineWidth', 2.0, 'MarkerSize', 5.5, ...
            'MarkerFaceColor', orange, 'MarkerEdgeColor', 'none', ...
            'DisplayName', 'UACI (right axis)');
        yline(ax, panels{p, 6}, '--', sprintf('ideal %.4f', panels{p, 6}), ...
            'Color', orange, 'LineWidth', 1.0, 'Alpha', 0.55, 'FontSize', 10, ...
            'LabelHorizontalAlignment', 'left', 'LabelVerticalAlignment', 'bottom', ...
            'HandleVisibility', 'off');
        ylim(ax, uaci_lims);
        if p == 2
            ylabel(ax, 'UACI (%)');
        else
            yticklabels(ax, []);
        end

        xlim(ax, [6 402]);
        xticks(ax, [24 48:48:384]);
        xlabel(ax, 'K (state bits)');
        title(ax, panels{p, 4}, 'FontWeight', 'normal', 'FontSize', 12.5);

        if p == 1
            lg = legend(ax, 'Orientation', 'horizontal');
            lg.FontSize = 11;
            lg.Box = 'off';
            lg.Layout.Tile = 'south';
        end
    end

    layout_title(tl, { ...
        'NPCR and UACI: RGB888 vs grayscale', ...
        'each panel vs its OWN ideal: RGB888 on L=2^{24}, grayscale on L=2^{8}'}, ink);
    export_figure(fh, out_stem);
end

% ---------------------------------------------------------------------------

function m = group_means(k, v, K_VALUES)
    m = arrayfun(@(K) mean(v(k == K)), K_VALUES);
end

function c = tint(rgb)
    c = rgb + (1 - rgb) * 0.42;
end

function lims = shared_limits(all_values, ideals)
% Limits covering every point from BOTH sweeps plus every ideal line.
    lo = min([all_values(:); ideals(:)]);
    hi = max([all_values(:); ideals(:)]);
    span = hi - lo;
    if span <= 0
        span = max(abs(hi), 1) * 1e-6;
    end
    lims = [lo - 0.08 * span, hi + 0.08 * span];
end

function fh = new_figure(width_in, height_in)
    fh = figure('Visible', 'off', 'Units', 'inches', ...
        'Position', [1 1 width_in height_in], 'Color', 'w');
    % R2025a+ figures inherit the desktop theme, which renders the axes on
    % a near-black background. Pin these print figures to the light theme.
    try %#ok<TRYNC>
        theme(fh, 'light');
    end
    set(fh, 'Color', 'w');
    set(fh, 'PaperUnits', 'inches', 'PaperPosition', [0 0 width_in height_in], ...
        'PaperSize', [width_in height_in]);
end

function layout_title(tl, lines, ink)
% Main title plus a smaller subtitle line. Kept as separate objects: a
% single long tiledlayout title gets truncated.
    title(tl, lines{1}, 'FontSize', 15, 'FontWeight', 'bold', 'Color', ink);
    if numel(lines) > 1
        subtitle(tl, lines{2}, 'FontSize', 11, 'FontWeight', 'normal', ...
            'Color', [0.32 0.32 0.32]);
    end
end

function style_axes(ax, ink)
    ax.FontSize = 12;
    ax.LineWidth = 0.9;
    ax.XColor = ink;
    ax.YColor = ink;
    ax.GridAlpha = 0.12;
    ax.Box = 'off';
    ax.TickDir = 'out';
    grid(ax, 'on');
end

function export_figure(fh, out_stem)
% Vector PDF only, by project policy and because it is the only export
% path that renders these figures faithfully: MATLAB R2026a's raster
% export drops primitives in the second tile of a 1x2 yyaxis layout (the
% whole right-hand point cloud and its line markers, plus broken glyphs).
% The vector path is unaffected.
    exportgraphics(fh, [out_stem '.pdf'], 'ContentType', 'vector', 'BackgroundColor', 'white');
    close(fh);
    fprintf('  %s.pdf (vector)\n', out_stem);
end
