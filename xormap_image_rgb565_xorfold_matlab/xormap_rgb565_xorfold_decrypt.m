function [plain_packed, iterations] = xormap_rgb565_xorfold_decrypt(cipher_packed, seed)
%XORMAP_RGB565_XORFOLD_DECRYPT Invert the folded RGB565 stream cipher.
%   XOR is self-inverse, so the same seed regenerates the same one-word-
%   per-state keystream.  As in the existing image experiments, this is a
%   round-trip check rather than a complete key/seed transport protocol.

    if ~ismatrix(cipher_packed) || ~isa(cipher_packed, 'uint16')
        error('xormap_rgb565_xorfold_decrypt:badImage', ...
            'cipher_packed must be a 2-D uint16 RGB565 image');
    end

    [rows, cols] = size(cipher_packed);
    cipher_words = reshape(cipher_packed.', 1, rows * cols);
    [keystream_words, ~, iterations] = ...
        xormap_keystream_xorfold16(seed, numel(cipher_words));
    plain_words = bitxor(cipher_words, keystream_words);
    plain_packed = reshape(plain_words, cols, rows).';
end
