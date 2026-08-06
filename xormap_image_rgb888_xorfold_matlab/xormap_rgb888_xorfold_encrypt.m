function [cipher, seed, iterations] = xormap_rgb888_xorfold_encrypt(plain_packed, key_bits)
%XORMAP_RGB888_XORFOLD_ENCRYPT Encrypt packed RGB888 one state per pixel.
%   [CIPHER, SEED, ITERATIONS] = XORMAP_RGB888_XORFOLD_ENCRYPT(PLAIN, KEY)
%   derives the same plaintext-related K-bit seed as the non-fold RGB888
%   experiment, but advances xormap exactly once per pixel and folds the
%   complete next state to the pixel's 24-bit XOR mask (xor_fold24.m).

    if ~ismatrix(plain_packed) || ~isa(plain_packed, 'uint32')
        error('xormap_rgb888_xorfold_encrypt:badImage', ...
            'plain_packed must be a 2-D uint32 RGB888 image');
    end
    if ~isvector(key_bits) || numel(key_bits) <= 4 || mod(numel(key_bits), 24) ~= 0
        error('xormap_rgb888_xorfold_encrypt:badKey', ...
            'key_bits must be a bit vector with K > 4 and K a multiple of 24');
    end
    if ~islogical(key_bits) && any(key_bits(:) ~= 0 & key_bits(:) ~= 1)
        error('xormap_rgb888_xorfold_encrypt:badKey', 'key_bits values must be binary');
    end

    [rows, cols] = size(plain_packed);
    plain_words = reshape(plain_packed.', 1, rows * cols);
    k = numel(key_bits);

    hash_bits = hash_expand_bits(words24_to_bytes_be(plain_words), k);
    seed = xor(logical(key_bits(:)).', hash_bits);

    [keystream_words, ~, iterations] = ...
        xormap_keystream_xorfold24(seed, numel(plain_words));
    cipher_words = bitxor(plain_words, keystream_words);
    cipher = reshape(cipher_words, cols, rows).';
end
