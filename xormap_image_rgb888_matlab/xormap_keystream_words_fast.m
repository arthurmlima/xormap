function words = xormap_keystream_words_fast(seed_bits, word_width, num_words)
%XORMAP_KEYSTREAM_WORDS_FAST Generate NUM_WORDS keystream words of
%   WORD_WIDTH bits each by iterating xormap_transform from SEED_BITS.
%   Word-width generalization of xormap_image_rgb565_matlab's
%   xormap_keystream_words.m (which concatenates K-bit iterations and
%   slices an arbitrary prefix), but built on XORMAP_TRANSFORM_FAST
%   (../xormap_matlab) instead of the canonical O(K^2) xormap_transform,
%   since the K=24:24:384 sweep over every SIPI color image (51 images,
%   ~35M pixels) is impractical at O(K^2) per state update.
%   XORMAP_TRANSFORM_FAST is exactly sequence-equivalent to
%   xormap_transform (test_xormap_rgb888.m proves it per-K over every
%   single-bit basis state), so this produces bit-for-bit the same
%   keystream xormap_keystream_words would, just faster.
%
%   Each iteration advances the register exactly the way Genv_xormap.m's
%   `en` pulse does. Returned as uint32.

    k = numel(seed_bits);
    plan = xormap_fast_plan(k);

    num_bits_needed = num_words * word_width;
    num_iters = ceil(num_bits_needed / k);

    stream = false(1, num_iters * k);
    v = logical(seed_bits(:)).';
    for it = 1:num_iters
        v = xormap_transform_fast(v, plan);
        stream((it - 1) * k + 1 : it * k) = v;
    end

    words = bits_to_words(stream(1:num_bits_needed), word_width);
end
