function bytes = xormap_keystream_fast(seed_bits, num_bytes)
%XORMAP_KEYSTREAM_FAST Drop-in fast replacement for XORMAP_KEYSTREAM.
%   BYTES = XORMAP_KEYSTREAM_FAST(SEED_BITS, NUM_BYTES) produces exactly
%   the same keystream as XORMAP_KEYSTREAM -- same register recurrence,
%   same K-bit-per-iteration concatenation, same truncation -- but built on
%   XORMAP_TRANSFORM_FAST (../xormap_matlab), the prefix-XOR
%   reformulation of the canonical O(K^2) xormap_transform.
%
%   XORMAP_TRANSFORM_FAST is sequence-equivalent to xormap_transform by
%   construction and is proved so per-K over every single-bit basis state
%   by the sibling projects' tests; test_xormap_gray_fast.m re-proves the
%   equivalence at this project's keystream level.
%
%   Needed because the all-grayscale sweep (159 SIPI images, ~51.7M pixels,
%   K = 24:24:384, two encryptions each) is impractical at O(K^2) per state
%   update -- the same reason xormap_image_rgb888_matlab reaches for
%   xormap_keystream_words_fast.
%
%   Each iteration still yields K bits, i.e. K/8 pixels of an 8-bit
%   grayscale image, so the iteration count SHRINKS as K grows: a wider
%   register buys proportionally more keystream per state update.

    k = numel(seed_bits);
    plan = xormap_fast_plan(k);

    num_bits_needed = num_bytes * 8;
    num_iters = ceil(num_bits_needed / k);

    stream = false(1, num_iters * k);
    v = logical(seed_bits(:)).';

    % Indices hoisted out of the loop: at K=24 a 1024x1024 image needs
    % ~350k iterations, so per-iteration overhead is what dominates.
    left = plan.left;
    right_plus_1 = plan.right + 1;
    odd_positions = plan.odd_positions;
    centers = plan.centers;

    for it = 1:num_iters
        % Inlined XORMAP_TRANSFORM_FAST: the public helper re-validates its
        % arguments on every call, which is worth it for one-off use and
        % pure overhead in a hot loop like this one.
        prefix = [false, mod(cumsum(v), 2) ~= 0];
        next_v = xor(prefix(right_plus_1), prefix(left));
        next_v(odd_positions) = xor(next_v(odd_positions), v(centers));
        v = next_v;

        stream((it - 1) * k + 1 : it * k) = v;
    end

    bytes = bits_to_bytes(stream(1:num_bits_needed));
end
