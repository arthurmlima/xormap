function plan = xormap_fast_plan(k)
%XORMAP_FAST_PLAN Precompute indices for XORMAP_TRANSFORM_FAST.
%   PLAN = XORMAP_FAST_PLAN(K) describes the same pairwise-XOR windows as
%   ../xormap_matlab/xormap_transform.m.  The plan lets repeated state
%   updates use prefix XORs, reducing each update from O(K^2) pair visits
%   to O(K) work.  No PRNG or cipher semantics are changed.

    if ~isscalar(k) || ~isnumeric(k) || ~isfinite(k) || k ~= floor(k) || k <= 4
        error('xormap_fast_plan:invalidK', 'K must be an integer greater than 4');
    end

    start_col = (k - 1) - floor(k / 2);
    columns = start_col + (0:(k - 1));

    left = ones(1, k);
    right = zeros(1, k);
    first_section = columns <= (k - 1);

    left(first_section) = k - columns(first_section);
    right(first_section) = k;
    right(~first_section) = 2 * k - 1 - columns(~first_section);

    window_length = right - left + 1;
    odd_positions = find(mod(window_length, 2) == 1);
    centers = (left(odd_positions) + right(odd_positions)) / 2;

    plan = struct( ...
        'k', k, ...
        'left', left, ...
        'right', right, ...
        'odd_positions', odd_positions, ...
        'centers', centers);
end
