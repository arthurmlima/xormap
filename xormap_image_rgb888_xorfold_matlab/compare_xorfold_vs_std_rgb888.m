function compare_xorfold_vs_std_rgb888()
%COMPARE_XORFOLD_VS_STD_RGB888 High-resolution side-by-side comparison figures.
%   Plots the RGB888 XOR-fold sweep (this folder) against the non-fold
%   RGB888 sweep (../xormap_image_rgb888_matlab) for every metric the two
%   experiments share. Both sweeps cover the same 51 SIPI color images at
%   K = 24:24:384, so the (image, K) grids line up one-for-one.
%
%   Each figure is one metric: XOR-fold on the left, standard on the right,
%   with IDENTICAL x and y limits on both panels so panel-to-panel
%   differences are real differences and not rescaling. Faint gray dots are
%   the 51 individual images; the bold line is the mean over them.
%
%   The timing columns are deliberately NOT plotted: the two CSVs were
%   produced on different machines, so their seconds are not comparable.
%   Use iterations_per_encrypt for a machine-independent cost comparison.
%
%   Writes 600-dpi PNG plus vector PDF into results/comparison/.

    this_dir = fileparts(mfilename('fullpath'));
    addpath(fullfile(this_dir, '..', 'xormap_image_matlab'));   % npcr_uaci_ideal

    fold_csv = fullfile(this_dir, 'results', 'sweep_k_xorfold_rgb888.csv');
    std_csv  = fullfile(this_dir, '..', 'xormap_image_rgb888_matlab', ...
                        'results', 'sweep_k_rgb888.csv');
    for p = {fold_csv, std_csv}
        if ~exist(p{1}, 'file')
            error('compare_xorfold_vs_std_rgb888:missingCsv', ...
                'Missing %s -- run the corresponding sweep first.', p{1});
        end
    end

    fold = readtable(fold_csv);
    std_ = readtable(std_csv);

    K_VALUES = (24:24:384).';
    assert(isequal(unique(fold.K), K_VALUES) && isequal(unique(std_.K), K_VALUES), ...
        'both sweeps must cover K = 24:24:384');

    out_dir = fullfile(this_dir, 'results', 'comparison');
    if ~exist(out_dir, 'dir')
        mkdir(out_dir);
    end

    % Palette: categorical slots 1 and 2 of the validated reference palette
    % (all-pairs CVD dE 24.7, normal-vision dE 33.6 -- safe for print and
    % for the common colour-vision deficiencies).
    BLUE   = [0.165 0.471 0.839];   % #2a78d6
    ORANGE = [0.922 0.408 0.204];   % #eb6834
    GRAY   = [0.62 0.62 0.60];
    INK    = [0.043 0.043 0.043];

    [npcr_ideal, uaci_ideal] = npcr_uaci_ideal(24);

    % --- single-metric figures -------------------------------------------
    make_single_metric_figure( ...
        K_VALUES, fold.K, fold.mean_entropy_cipher, std_.K, std_.mean_entropy_cipher, ...
        'Mean Shannon entropy of cipher (bits/channel)', 8.0, 'ideal = 8', ...
        {'RGB888 cipher entropy: XOR-fold vs standard', ...
         'identical axis limits on both panels \bullet 51 SIPI colour images \bullet K = 24:24:384'}, ...
        fullfile(out_dir, 'compare_entropy'), BLUE, GRAY, INK);

    make_single_metric_figure( ...
        K_VALUES, fold.K, fold.mean_abs_corrH_cipher, std_.K, std_.mean_abs_corrH_cipher, ...
        'Mean |horizontal correlation| of cipher', 0.0, 'ideal = 0', ...
        {'RGB888 adjacent-pixel correlation: XOR-fold vs standard', ...
         'identical axis limits on both panels \bullet 51 SIPI colour images \bullet K = 24:24:384'}, ...
        fullfile(out_dir, 'compare_correlation'), BLUE, GRAY, INK);

    % --- combined NPCR / UACI figure (dual y-axis, as requested) ---------
    make_npcr_uaci_figure(K_VALUES, fold, std_, npcr_ideal, uaci_ideal, ...
        fullfile(out_dir, 'compare_npcr_uaci'), BLUE, ORANGE, INK);

    fprintf('Wrote comparison figures to %s\n', out_dir);
end

% ---------------------------------------------------------------------------

function make_single_metric_figure(K_VALUES, kf, vf, ks, vs, ylab, ideal, ...
                                   ideal_label, super_title, out_stem, ...
                                   series_color, gray, ink)
% One metric, two panels, shared limits.

    ylims = shared_limits([vf; vs], ideal);

    fh = new_figure(13.5, 5.4);
    tl = tiledlayout(fh, 1, 2, 'TileSpacing', 'compact', 'Padding', 'compact');

    panels = { ...
        kf, vf, 'XOR-fold: one folded K-bit state per pixel'; ...
        ks, vs, 'Standard: K-bit state sliced into K/24 pixels'};

    for p = 1:2
        ax = nexttile(tl);
        hold(ax, 'on');
        style_axes(ax, ink);

        yline(ax, ideal, '--', ideal_label, 'Color', [0.35 0.35 0.35], ...
            'LineWidth', 1.0, 'FontSize', 11, 'LabelHorizontalAlignment', 'right', ...
            'LabelVerticalAlignment', 'bottom', 'HandleVisibility', 'off');

        scatter(ax, panels{p, 1}, panels{p, 2}, 11, gray, 'filled', ...
            'MarkerFaceAlpha', 0.30, 'MarkerEdgeColor', 'none', ...
            'DisplayName', 'individual images (51)');

        m = group_means(panels{p, 1}, panels{p, 2}, K_VALUES);
        plot(ax, K_VALUES, m, '-o', 'Color', series_color, 'LineWidth', 2.0, ...
            'MarkerSize', 5.5, 'MarkerFaceColor', series_color, ...
            'MarkerEdgeColor', 'none', 'DisplayName', 'mean over 51 images');

        xlim(ax, [12 396]);
        ylim(ax, ylims);
        xticks(ax, [24 48:48:384]);   % label K=24 explicitly: it is the outlier
        xlabel(ax, 'K (state bits)');
        if p == 1
            ylabel(ax, ylab);
            % Park the legend in the layout's own strip: inside the axes it
            % collides with the ideal-line label at the bottom right.
            lg = legend(ax, 'Orientation', 'horizontal');
            lg.FontSize = 11;
            lg.Box = 'off';
            lg.Layout.Tile = 'south';
        else
            % Shared scale: keep the ticks, drop the duplicated tick labels
            % so the eye reads both panels against the left-hand axis.
            yticklabels(ax, []);
        end
        title(ax, panels{p, 3}, 'FontWeight', 'normal', 'FontSize', 12.5);
    end

    layout_title(tl, super_title, ink);
    export_figure(fh, out_stem);
end

% ---------------------------------------------------------------------------

function make_npcr_uaci_figure(K_VALUES, fold, std_, npcr_ideal, uaci_ideal, ...
                               out_stem, blue, orange, ink)
% NPCR and UACI share a panel: NPCR on the left axis, UACI on the right.
% Both panels use identical left and right limits.
%
% The per-image dots are tinted with their own series colour rather than a
% neutral gray: with two independent y-scales in one panel, a gray cloud
% gives no clue which axis it belongs to.

    npcr_lims = shared_limits([fold.npcr_packed; std_.npcr_packed], npcr_ideal);
    uaci_lims = shared_limits([fold.uaci_packed; std_.uaci_packed], uaci_ideal);

    fh = new_figure(14.5, 5.8);
    tl = tiledlayout(fh, 1, 2, 'TileSpacing', 'compact', 'Padding', 'compact');

    panels = {fold, 'XOR-fold: one folded K-bit state per pixel'; ...
              std_, 'Standard: K-bit state sliced into K/24 pixels'};

    for p = 1:2
        T = panels{p, 1};
        ax = nexttile(tl);
        style_axes(ax, ink);

        % --- left axis: NPCR ---
        yyaxis(ax, 'left');
        hold(ax, 'on');
        ax.YAxis(1).Color = blue;
        scatter(ax, T.K, T.npcr_packed, 11, tint(blue), 'filled', ...
            'MarkerFaceAlpha', 0.45, 'MarkerEdgeColor', 'none', ...
            'HandleVisibility', 'off');
        mn = group_means(T.K, T.npcr_packed, K_VALUES);
        plot(ax, K_VALUES, mn, '-o', 'Color', blue, 'LineWidth', 2.0, ...
            'MarkerSize', 5.5, 'MarkerFaceColor', blue, 'MarkerEdgeColor', 'none', ...
            'DisplayName', 'NPCR (left axis)');
        % Label each ideal line only in the panel that carries that axis's
        % tick labels -- a long layout subtitle gets truncated, so the
        % reference values live on the lines themselves.
        if p == 1
            yline(ax, npcr_ideal, '--', sprintf('ideal %.5f', npcr_ideal), ...
                'Color', blue, 'LineWidth', 1.0, 'Alpha', 0.55, 'FontSize', 10, ...
                'LabelHorizontalAlignment', 'right', 'LabelVerticalAlignment', 'bottom', ...
                'HandleVisibility', 'off');
        else
            yline(ax, npcr_ideal, '--', 'Color', blue, 'LineWidth', 1.0, ...
                'Alpha', 0.55, 'HandleVisibility', 'off');
        end
        ylim(ax, npcr_lims);
        if p == 1
            ylabel(ax, 'NPCR, packed 24-bit word (%)');
        else
            yticklabels(ax, []);
        end

        % --- right axis: UACI ---
        yyaxis(ax, 'right');
        hold(ax, 'on');
        ax.YAxis(2).Color = orange;
        scatter(ax, T.K, T.uaci_packed, 11, tint(orange), 'filled', ...
            'MarkerFaceAlpha', 0.45, 'MarkerEdgeColor', 'none', ...
            'HandleVisibility', 'off');
        mu = group_means(T.K, T.uaci_packed, K_VALUES);
        plot(ax, K_VALUES, mu, '-s', 'Color', orange, 'LineWidth', 2.0, ...
            'MarkerSize', 5.5, 'MarkerFaceColor', orange, 'MarkerEdgeColor', 'none', ...
            'DisplayName', 'UACI (right axis)');
        if p == 2
            yline(ax, uaci_ideal, '--', sprintf('ideal %.4f', uaci_ideal), ...
                'Color', orange, 'LineWidth', 1.0, 'Alpha', 0.55, 'FontSize', 10, ...
                'LabelHorizontalAlignment', 'left', 'LabelVerticalAlignment', 'bottom', ...
                'HandleVisibility', 'off');
        else
            yline(ax, uaci_ideal, '--', 'Color', orange, 'LineWidth', 1.0, ...
                'Alpha', 0.55, 'HandleVisibility', 'off');
        end
        ylim(ax, uaci_lims);
        if p == 2
            ylabel(ax, 'UACI, packed 24-bit word (%)');
        else
            yticklabels(ax, []);
        end

        xlim(ax, [12 396]);
        xticks(ax, [24 48:48:384]);   % label K=24 explicitly: it is the outlier
        xlabel(ax, 'K (state bits)');
        title(ax, panels{p, 2}, 'FontWeight', 'normal', 'FontSize', 12.5);

        if p == 1
            lg = legend(ax, 'Orientation', 'horizontal');
            lg.FontSize = 11;
            lg.Box = 'off';
            lg.Layout.Tile = 'south';
        end
    end

    layout_title(tl, { ...
        'RGB888 NPCR and UACI: XOR-fold vs standard', ...
        'NPCR left axis \bullet UACI right axis \bullet dashed = ideal \bullet 51 SIPI images'}, ink);
    export_figure(fh, out_stem);
end

% ---------------------------------------------------------------------------

function m = group_means(k, v, K_VALUES)
    m = arrayfun(@(K) mean(v(k == K)), K_VALUES);
end

function c = tint(rgb)
% Lighten a series colour toward the surface for the recessive point cloud.
    c = rgb + (1 - rgb) * 0.42;
end

function lims = shared_limits(all_values, ideal)
% Limits covering every point from BOTH sweeps plus the ideal line, so the
% two panels can be compared without rescaling.
    lo = min([all_values(:); ideal]);
    hi = max([all_values(:); ideal]);
    span = hi - lo;
    if span <= 0
        span = max(abs(hi), 1) * 1e-6;
    end
    lims = [lo - 0.08 * span, hi + 0.08 * span];
end

function fh = new_figure(width_in, height_in)
    fh = figure('Visible', 'off', 'Units', 'inches', ...
        'Position', [1 1 width_in height_in], 'Color', 'w');
    % R2025a+ figures inherit the desktop theme, which renders the axes on a
    % near-black background. Pin these print figures to the light theme.
    try %#ok<TRYNC>
        theme(fh, 'light');
    end
    set(fh, 'Color', 'w');
    set(fh, 'PaperUnits', 'inches', 'PaperPosition', [0 0 width_in height_in], ...
        'PaperSize', [width_in height_in]);
end

function layout_title(tl, lines, ink)
% Main title plus a smaller subtitle line. Kept as separate objects rather
% than one long string: a single long tiledlayout title gets truncated.
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
% The vector PDF is the deliverable: it is resolution-independent and it is
% the only path that renders these figures correctly.
%
% MATLAB R2026a's high-DPI raster export silently DROPS marker and scatter
% primitives in the second tile of a 1x2 yyaxis layout -- verified on this
% machine: identical figure, 300 dpi keeps every point, 600 dpi loses the
% whole right-hand point cloud and the right panel's line markers. It is
% not exportgraphics-specific (print -dpng -r600 loses them too), and the
% vector path is unaffected. So: PDF at full fidelity, PNG capped at the
% resolution that still renders everything.
    exportgraphics(fh, [out_stem '.pdf'], 'ContentType', 'vector', 'BackgroundColor', 'white');
    close(fh);
    fprintf('  %s.pdf (vector)\n', out_stem);
end
