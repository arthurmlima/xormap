function words = bits_to_words(bits, word_width)
%BITS_TO_WORDS Pack a bit vector into words of WORD_WIDTH bits each.
%   WORDS = BITS_TO_WORDS(BITS, WORD_WIDTH) is the word-width
%   generalization of xormap_image_matlab's BITS_TO_BYTES (word_width=8
%   there): bit 1 of each word is its LSB. numel(BITS) must be a multiple
%   of WORD_WIDTH. Returned as uint32 (safely holds up to 32-bit words);
%   cast down (e.g. to uint16 for RGB565) as needed.

    if mod(numel(bits), word_width) ~= 0
        error('bits_to_words:badLength', 'numel(bits) must be a multiple of word_width');
    end

    n = numel(bits) / word_width;
    words = zeros(1, n, 'uint32');
    for idx = 1:n
        base = (idx - 1) * word_width;
        v = uint32(0);
        for b = 0:(word_width - 1)
            if bits(base + b + 1)
                v = bitset(v, b + 1);
            end
        end
        words(idx) = v;
    end
end
