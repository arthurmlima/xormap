function [m m1 m2] = extract_3section(M, k, l)
% ------------------------------------------------------------
% Extract k-column section from M (size r x (2k-3))
% centered at column (k-1) + l
%
% If k is even:
%   takes one more column on the LEFT side
%
% INPUTS:
%   M : input matrix  (r x (2k-3))
%   k : window size
%   l : offset from centre
%
% OUTPUT:
%   m : extracted matrix (r x k)
% ------------------------------------------------------------

    [~, N] = size(M);

    % sanity check
    if N ~= 2*k - 3
        error('M must have 2k-3 columns');
    end

    % centre column
    c = k - 1;

    % left/right reach
    L = floor(k/2);     % left columns
    R = k - L - 1;      % right columns
                        % (this ensures even k is biased left)

    % compute window limits
    start_col = c - L + l;
    end_col   = c + R + l;

    % bounds check
    if start_col < 1 || end_col > N
        error('Offset l moves window outside matrix bounds');
    end

    % extract
    m = M(:, start_col:end_col);

    m1 = M(:, 1:start_col-1);
   
    m2 = M(:, end_col+1:N);


end