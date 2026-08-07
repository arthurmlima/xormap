function [words, final_state, iterations] = xormap_keystream_xorfold24(seed_bits, num_pixels)
%XORMAP_KEYSTREAM_XORFOLD24 Generate one folded PRNG word per pixel.
%   [WORDS, FINAL_STATE, ITERATIONS] = XORMAP_KEYSTREAM_XORFOLD24(SEED_BITS,
%   NUM_PIXELS) performs exactly NUM_PIXELS state transitions. After each
%   transition, the complete K-bit state is XOR-folded into one uint32
%   word (low 24 bits meaningful). Thus one PRNG iteration encrypts one
%   RGB888 pixel for every K:
%
%       state = xormap_transform(state, K)
%       words(pixel) = xor_fold24(state)
%
%   K must be a multiple of 24. The requested experiment uses
%   K = 24:24:384. XORMAP_TRANSFORM_FAST is sequence-equivalent to the
%   reference XORMAP_TRANSFORM and is used to keep full-database runs
%   practical (see ../xormap_image_rgb565_xorfold_matlab).
%
%   24-bit analogue of the sibling RGB565 project's
%   xormap_keystream_xorfold16.m.

    if ~isvector(seed_bits) || numel(seed_bits) <= 4
        error('xormap_keystream_xorfold24:badSeed', ...
            'seed_bits must be a bit vector longer than 4 bits');
    end
    if ~islogical(seed_bits) && any(seed_bits(:) ~= 0 & seed_bits(:) ~= 1)
        error('xormap_keystream_xorfold24:badSeed', 'seed_bits values must be binary');
    end

    k = numel(seed_bits);
    if mod(k, 24) ~= 0
        error('xormap_keystream_xorfold24:badK', ...
            'K must be a multiple of 24 for a complete 24-bit XOR fold');
    end
    if ~isscalar(num_pixels) || ~isnumeric(num_pixels) || ~isfinite(num_pixels) || ...
            num_pixels < 0 || num_pixels ~= floor(num_pixels)
        error('xormap_keystream_xorfold24:badCount', ...
            'num_pixels must be a nonnegative integer scalar');
    end

    state = logical(seed_bits(:)).';
    plan = xormap_fast_plan(k);
    weights = 2 .^ (0:23);
    words = zeros(1, num_pixels, 'uint32');
    iterations = 0;

    for pixel = 1:num_pixels
        % Inline XORMAP_TRANSFORM_FAST in this hot loop. The public
        % helper performs input validation that is useful for individual
        % calls but unnecessarily expensive once per image pixel.
        prefix = [false, mod(cumsum(state), 2) ~= 0];
        next_state = xor(prefix(plan.right + 1), prefix(plan.left));
        next_state(plan.odd_positions) = xor( ...
            next_state(plan.odd_positions), state(plan.centers));
        state = next_state;

        % Inline XOR_FOLD24 in this hot loop: each column is one 24-bit
        % chunk, and every chunk is reduced into the same output lanes.
        folded_bits = mod(sum(reshape(state, 24, []), 2), 2);
        words(pixel) = uint32(weights * folded_bits);
        iterations = iterations + 1;
    end

    final_state = state;
end
