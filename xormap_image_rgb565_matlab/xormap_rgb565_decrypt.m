function plain_packed = xormap_rgb565_decrypt(cipher_packed, seed)
%XORMAP_RGB565_DECRYPT Inverse of XORMAP_RGB565_ENCRYPT given the SEED
%   that was used. XOR is self-inverse, so this regenerates the same
%   keystream from SEED and XORs it back off. As with
%   xormap_image_matlab's xormap_image_decrypt, this only checks the
%   stream-cipher core is correct -- SEED is not re-derived from
%   CIPHER_PACKED, since it depends on the plaintext hash.

    [rows, cols] = size(cipher_packed);
    cipher_words = reshape(cipher_packed.', 1, rows * cols);

    keystream_words = uint16(xormap_keystream_words(seed, 16, numel(cipher_words)));
    plain_words = bitxor(cipher_words, keystream_words);

    plain_packed = reshape(plain_words, cols, rows).';
end
