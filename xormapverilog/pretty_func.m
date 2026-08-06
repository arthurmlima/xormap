function pretty_func(M)
% PRETTY_FUNC  Pretty-print a cell array of [y z] pairs as a grid.
%   Repeated pairs (counting [a b] and [b a] as the same) are highlighted:
%     - printed in red with ANSI colour (works in most terminals)
%     - and marked with a trailing asterisk *
%   Empty cells print as a dot.
%
%   Usage:  pretty_func(M)

    [R, C] = size(M);

    % ---- first pass: canonical key per cell + tally of occurrences ----
    keyGrid = cell(R, C);
    counts  = containers.Map('KeyType','char','ValueType','double');

    for c = 1:C
        for r = 1:R
            v = M{r,c};
            if isnumeric(v) && numel(v) == 2
                p = sort(double(v(:)).');
                key = sprintf('%d_%d', p(1), p(2));
                keyGrid{r,c} = key;
                if isKey(counts, key)
                    counts(key) = counts(key) + 1;
                else
                    counts(key) = 1;
                end
            end
        end
    end

    % ---- work out column widths for alignment ----
    colW = zeros(1, C);
    for c = 1:C
        for r = 1:R
            colW(c) = max(colW(c), numel(cellstr_for(M{r,c})));
        end
        colW(c) = max(colW(c), 1);
    end

    % ANSI colour codes
    RED   = char(27) + "[31m";   % repeated
    RESET = char(27) + "[0m";

    nDup = 0;

    % ---- second pass: print ----
    fprintf('\n');
    for r = 1:R
        for c = 1:C
            s   = cellstr_for(M{r,c});
            key = keyGrid{r,c};

            isDup = ~isempty(key) && counts(key) > 1;
            if isDup, nDup = nDup + 1; end

            % pad to column width (right-aligned)
            pad = repmat(' ', 1, colW(c) - numel(s));
            cellTxt = [pad, s];

            if isDup
                fprintf('  %s%s*%s', RED, cellTxt, RESET);
            else
                fprintf('  %s ', cellTxt);   % trailing space where * would be
            end
        end
        fprintf('\n');
    end

    fprintf('\n');
    if nDup > 0
        fprintf('%s*%s = repeated pair (%d cell(s) involved).\n', RED, RESET, nDup);
    else
        fprintf('No repeated pairs — every tuple is unique.\n');
    end
    fprintf('\n');
end

% ------------------------------------------------------------------
function s = cellstr_for(v)
% Format one cell's contents as a compact string.
    if isnumeric(v) && numel(v) == 2
        s = sprintf('%d %d', v(1), v(2));
    elseif isempty(v)
        s = '.';
    else
        s = mat2str(v);
    end
end
