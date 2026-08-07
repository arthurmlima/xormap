function words = bits_to_words(bits, word_width)
%BITS_TO_WORDS Pack a bit vector into words of WORD_WIDTH bits each.
%   WORDS = BITS_TO_WORDS(BITS, WORD_WIDTH) is the word-width
%   generalization of xormap_image_matlab's BITS_TO_BYTES (word_width=8
%   there): bit 1 of each word is its LSB. numel(BITS) must be a multiple
%   of WORD_WIDTH. Returned as uint32 (safely holds up to 32-bit words);
%   cast down (e.g. to uint16 for RGB565) as needed.
%
%   Vectorised the same way as BITS_TO_BYTES: each word is a weighted sum
%   of its bits, so the whole stream is one 1xW * WxN matrix product. The
%   previous per-bit loop with a bitset call ran WORD_WIDTH interpreted
%   operations per word (24 per pixel for RGB888), which dominated the
%   database-wide sweeps. Weights up to 2^31 and their sums are exactly
%   representable in double, so the uint32 cast is lossless.

    if mod(numel(bits), word_width) ~= 0
        error('bits_to_words:badLength', 'numel(bits) must be a multiple of word_width');
    end
    if word_width > 32
        error('bits_to_words:tooWide', 'word_width must be at most 32');
    end

    grouped = reshape(logical(bits(:)), word_width, []);   % column j = word j, LSB first
    words = uint32((2 .^ (0:(word_width - 1))) * grouped); % 1xW * WxN -> 1xN
end
