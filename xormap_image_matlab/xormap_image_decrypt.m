function plain = xormap_image_decrypt(cipher, seed)
%XORMAP_IMAGE_DECRYPT Inverse of XORMAP_IMAGE_ENCRYPT given the SEED that
%   was used. XOR is self-inverse, so this regenerates the same keystream
%   from SEED and XORs it back off. This only checks the stream-cipher
%   core is correct -- it does not re-derive SEED from CIPHER, since SEED
%   depends on the *plaintext* hash; a real receiver would need SEED
%   delivered out of band.

    [rows, cols] = size(cipher);
    cipher_bytes = reshape(cipher.', 1, rows * cols);

    keystream_bytes = xormap_keystream(seed, numel(cipher_bytes));
    plain_bytes = bitxor(cipher_bytes, keystream_bytes);

    plain = reshape(plain_bytes, cols, rows).';
end
