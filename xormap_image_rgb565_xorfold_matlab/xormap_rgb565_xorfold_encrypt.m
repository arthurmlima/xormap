function [cipher, seed, iterations] = xormap_rgb565_xorfold_encrypt(plain_packed, key_bits)
%XORMAP_RGB565_XORFOLD_ENCRYPT Encrypt packed RGB565 one state per pixel.
%   [CIPHER, SEED, ITERATIONS] = XORMAP_RGB565_XORFOLD_ENCRYPT(PLAIN, KEY)
%   derives the same plaintext-related K-bit seed as the existing RGB565
%   experiment, but advances xormap exactly once for each pixel and folds
%   the complete next state to the pixel's 16-bit XOR mask.

    if ~ismatrix(plain_packed) || ~isa(plain_packed, 'uint16')
        error('xormap_rgb565_xorfold_encrypt:badImage', ...
            'plain_packed must be a 2-D uint16 RGB565 image');
    end
    if ~isvector(key_bits) || numel(key_bits) <= 4 || mod(numel(key_bits), 16) ~= 0
        error('xormap_rgb565_xorfold_encrypt:badKey', ...
            'key_bits must be a bit vector with K > 4 and K a multiple of 16');
    end
    if ~islogical(key_bits) && any(key_bits(:) ~= 0 & key_bits(:) ~= 1)
        error('xormap_rgb565_xorfold_encrypt:badKey', 'key_bits values must be binary');
    end

    [rows, cols] = size(plain_packed);
    plain_words = reshape(plain_packed.', 1, rows * cols);
    k = numel(key_bits);

    hash_bits = hash_expand_bits(words_to_bytes_be(plain_words), k);
    seed = xor(logical(key_bits(:)).', hash_bits);

    [keystream_words, ~, iterations] = ...
        xormap_keystream_xorfold16(seed, numel(plain_words));
    cipher_words = bitxor(plain_words, keystream_words);
    cipher = reshape(cipher_words, cols, rows).';
end
