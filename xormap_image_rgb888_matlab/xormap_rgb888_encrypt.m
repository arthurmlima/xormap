function [cipher, seed] = xormap_rgb888_encrypt(plain_packed, key_bits)
%XORMAP_RGB888_ENCRYPT XOR-stream-cipher a packed RGB888 image with xormap.
%   [CIPHER, SEED] = XORMAP_RGB888_ENCRYPT(PLAIN_PACKED, KEY_BITS) is the
%   24-bit/pixel analogue of xormap_image_rgb565_matlab's
%   xormap_rgb565_encrypt -- concatenating K-bit iterations and slicing
%   arbitrary-length prefixes, so K/24 pixels' worth of keystream come
%   out per iteration on average, same as K/8 for grayscale and K/16 for
%   RGB565. PLAIN_PACKED must be a 2-D uint32 RGB888 image (see
%   rgb888_pack.m; only the low 24 bits of each element are used).
%
%   Built on XORMAP_KEYSTREAM_WORDS_FAST (the fast O(K) transform) rather
%   than the naive O(K^2) transform its RGB565 sibling uses, since this
%   pipeline is meant to run across every SIPI color image (51 images,
%   ~35M pixels) -- see that function's docstring for the equivalence
%   argument.
%
%       SEED = KEY_BITS XOR HASH_EXPAND_BITS(bytes-of(PLAIN_PACKED), K)

    k = numel(key_bits);
    if k <= 4
        error('xormap_rgb888_encrypt:badKeyLength', 'key_bits length (K) must be > 4');
    end
    if ~ismatrix(plain_packed) || ~isa(plain_packed, 'uint32')
        error('xormap_rgb888_encrypt:badImage', 'plain_packed must be a 2-D uint32 RGB888 image');
    end

    [rows, cols] = size(plain_packed);
    plain_words = reshape(plain_packed.', 1, rows * cols);  % row-major flatten

    hash_bits = hash_expand_bits(words24_to_bytes_be(plain_words), k);
    seed = xor(logical(key_bits(:)).', hash_bits);

    keystream_words = xormap_keystream_words_fast(seed, 24, numel(plain_words));
    cipher_words = bitxor(plain_words, keystream_words);

    cipher = reshape(cipher_words, cols, rows).';
end
