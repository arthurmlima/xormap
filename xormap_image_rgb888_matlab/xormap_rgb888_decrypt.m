function plain_packed = xormap_rgb888_decrypt(cipher_packed, seed)
%XORMAP_RGB888_DECRYPT Inverse of XORMAP_RGB888_ENCRYPT given the SEED
%   that was used. XOR is self-inverse, so this regenerates the same
%   keystream from SEED and XORs it back off. As with the grayscale and
%   RGB565 decrypt functions, this only checks the stream-cipher core is
%   correct -- SEED is not re-derived from CIPHER_PACKED, since it
%   depends on the plaintext hash.

    [rows, cols] = size(cipher_packed);
    cipher_words = reshape(cipher_packed.', 1, rows * cols);

    keystream_words = xormap_keystream_words_fast(seed, 24, numel(cipher_words));
    plain_words = bitxor(cipher_words, keystream_words);

    plain_packed = reshape(plain_words, cols, rows).';
end
