function [T, meta] = read_sweep(source, options)
%READ_SWEEP Load a K-sweep results CSV into one normalised table.
%
%   T = READ_SWEEP('gray')      loads this project's all-grayscale sweep
%                               (results/sweep_k_gray_all.csv, 159 images)
%   T = READ_SWEEP('rgb888')    loads the colour sweep
%                               (../xormap_image_rgb888_matlab/results/
%                               sweep_k_rgb888.csv, 51 images)
%   T = READ_SWEEP('/path/to/some_sweep.csv')   any sweep CSV by path
%
%   [T, META] = READ_SWEEP(...) also returns the run's metadata.
%   READ_SWEEP(..., Summary=true) prints a per-K summary table as well.
%
%   WHY THIS EXISTS
%
%   1. The sweeps name the same quantities differently -- the colour sweep
%      writes mean_entropy_cipher / npcr_packed, the grayscale one writes
%      entropy_cipher / npcr. This returns ONE schema either way, so
%      callers never branch on which file they loaded.
%
%   2. Plain readtable() silently mis-parses the image column as DATETIME:
%      SIPI names like '4.1.01' and '5.1.09' look like dates, so
%      unique(T.image) then counts something meaningless. This pins that
%      column to text.
%
%   RETURNED COLUMNS
%
%     image        image name (string)
%     volume       SIPI volume (string; "" if the CSV doesn't record it)
%     K            keystream state width, bits
%     height, width, num_pixels
%     entropy      cipher entropy, bits per 8-bit channel
%                  (colour = mean over R,G,B; grayscale = the one channel)
%     corrH        mean |horizontal correlation| of the cipher
%     npcr, uaci   percent
%     seconds      wall clock for the two encryptions that row measured
%
%   META fields: name, path, symbol_bits, npcr_ideal, uaci_ideal,
%   num_images, num_rows, K_values.
%
%   COMPARING TWO SWEEPS -- read this before differencing npcr/uaci.
%   Entropy and corrH are directly comparable: both are per 8-bit channel,
%   ideals 8.0 and 0. NPCR/UACI are NOT on the same alphabet. The colour
%   sweep evaluates them on the packed 24-bit word (L=2^24), grayscale on
%   8-bit pixels (L=2^8), and both ideals depend on L:
%
%       NPCR_ideal = 100*(L-1)/L        UACI_ideal = 100*(L+1)/(3L)
%
%   so compare each against META.npcr_ideal / META.uaci_ideal, never
%   against each other. A 24-bit word counts as changed if ANY channel
%   changed, which is why colour NPCR sits near 100% and grayscale near
%   99.61% -- that gap is the alphabet, not cipher quality.
%
%   Example:
%       [g, gm] = read_sweep('gray', Summary=true);
%       mean(g.entropy(g.K == 384))
%       gm.npcr_ideal

    arguments
        source (1,1) string
        options.Summary (1,1) logical = false
    end

    this_dir = fileparts(mfilename('fullpath'));

    switch lower(source)
        case "gray"
            path = fullfile(this_dir, 'results', 'sweep_k_gray_all.csv');
            name = "grayscale (all SIPI grayscale images)";
        case "rgb888"
            path = fullfile(this_dir, '..', 'xormap_image_rgb888_matlab', ...
                            'results', 'sweep_k_rgb888.csv');
            name = "RGB888 (all SIPI colour images)";
        otherwise
            path = char(source);
            [~, stem] = fileparts(path);
            name = string(stem);
    end

    if ~exist(path, 'file')
        error('read_sweep:missingCsv', ...
            ['Sweep CSV not found: %s\n' ...
             'Run the corresponding sweep first ' ...
             '(sweep_k_gray_all.m or ../xormap_image_rgb888_matlab/sweep_k_rgb888.m).'], ...
            path);
    end

    raw = read_csv_with_text_names(path);
    vars = raw.Properties.VariableNames;

    % Which dialect is this? Detect by column name, so a CSV given by path
    % works without the caller having to say which sweep produced it.
    if all(ismember({'mean_entropy_cipher', 'npcr_packed'}, vars))
        map = struct('entropy', 'mean_entropy_cipher', ...
                     'corrH',   'mean_abs_corrH_cipher', ...
                     'npcr',    'npcr_packed', ...
                     'uaci',    'uaci_packed', ...
                     'seconds', 'seconds');
        symbol_bits = 24;   % NPCR/UACI measured on the packed 24-bit word
    elseif all(ismember({'entropy_cipher', 'npcr'}, vars))
        map = struct('entropy', 'entropy_cipher', ...
                     'corrH',   'abs_corrH_cipher', ...
                     'npcr',    'npcr', ...
                     'uaci',    'uaci', ...
                     'seconds', 'seconds_two_encryptions');
        symbol_bits = 8;    % NPCR/UACI measured on 8-bit pixels
    else
        error('read_sweep:unknownSchema', ...
            'Unrecognised sweep CSV schema in %s. Columns: %s', ...
            path, strjoin(vars, ', '));
    end

    T = table;
    T.image = string(raw.image);
    if ismember('volume', vars)
        T.volume = string(raw.volume);
    else
        T.volume = repmat("", height(raw), 1);
    end
    T.K          = raw.K;
    T.height     = raw.height;
    T.width      = raw.width;
    T.num_pixels = raw.num_pixels;
    T.entropy    = raw.(map.entropy);
    T.corrH      = raw.(map.corrH);
    T.npcr       = raw.(map.npcr);
    T.uaci       = raw.(map.uaci);
    T.seconds    = raw.(map.seconds);

    T = sortrows(T, {'K', 'image'});

    [npcr_ideal, uaci_ideal] = npcr_uaci_ideal(symbol_bits);
    meta = struct( ...
        'name',        name, ...
        'path',        string(path), ...
        'symbol_bits', symbol_bits, ...
        'npcr_ideal',  npcr_ideal, ...
        'uaci_ideal',  uaci_ideal, ...
        'num_images',  numel(unique(T.image)), ...
        'num_rows',    height(T), ...
        'K_values',    unique(T.K).');

    if options.Summary
        print_summary(T, meta);
    end
end

% ---------------------------------------------------------------------------

function T = read_csv_with_text_names(path)
% readtable guesses datetime for the image-name column, because SIPI names
% like '4.1.01' parse as dates. Pin it (and volume) to text.
    opts = detectImportOptions(path);
    for v = ["image", "volume"]
        if ismember(v, opts.VariableNames)
            opts = setvartype(opts, char(v), 'string');
        end
    end
    T = readtable(path, opts);
end

function print_summary(T, meta)
    fprintf('\n%s\n', meta.name);
    fprintf('  %s\n', meta.path);
    fprintf('  %d images x %d K values = %d rows\n', ...
        meta.num_images, numel(meta.K_values), meta.num_rows);
    fprintf(['  NPCR/UACI measured on %d-bit symbols ' ...
             '(ideal NPCR %.5f%%, UACI %.4f%%)\n\n'], ...
        meta.symbol_bits, meta.npcr_ideal, meta.uaci_ideal);

    fprintf('  %5s %10s %11s %12s %11s %10s\n', ...
        'K', 'entropy', '|corrH|', 'NPCR %', 'UACI %', 'sec');
    fprintf('  %5s %10s %11s %12s %11s %10s\n', ...
        '-----', '----------', '-----------', '------------', '-----------', '----------');
    for K = meta.K_values
        sel = T.K == K;
        fprintf('  %5d %10.6f %11.6f %12.6f %11.6f %10.1f\n', ...
            K, mean(T.entropy(sel)), mean(T.corrH(sel)), ...
            mean(T.npcr(sel)), mean(T.uaci(sel)), sum(T.seconds(sel)));
    end

    fprintf('\n  deviation of the all-K mean from ideal: ');
    fprintf('entropy %+.6f, |corrH| %+.6f, NPCR %+.6f, UACI %+.6f\n\n', ...
        mean(T.entropy) - 8.0, mean(T.corrH) - 0.0, ...
        mean(T.npcr) - meta.npcr_ideal, mean(T.uaci) - meta.uaci_ideal);
end
