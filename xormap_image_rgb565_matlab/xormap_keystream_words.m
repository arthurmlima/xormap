function words = xormap_keystream_words(seed_bits, word_width, num_words)
%XORMAP_KEYSTREAM_WORDS Generate NUM_WORDS keystream words of WORD_WIDTH
%   bits each by iterating xormap_transform from SEED_BITS -- the
%   word-width generalization of xormap_image_matlab's xormap_keystream
%   (word_width=8, producing bytes; here word_width=16 for RGB565 pixels).
%   Each iteration advances the register exactly the way Genv_xormap.m's
%   `en` pulse does (x_reg <= xormap_transform(x_reg)); K/WORD_WIDTH
%   pixels' worth of keystream come out per iteration. Returned as uint32.

    k = numel(seed_bits);
    num_bits_needed = num_words * word_width;
    num_iters = ceil(num_bits_needed / k);

    stream = false(1, num_iters * k);
    v = seed_bits;
    for it = 1:num_iters
        v = xormap_transform(v, k);
        stream((it - 1) * k + 1 : it * k) = v;
    end

    words = bits_to_words(stream(1:num_bits_needed), word_width);
end
