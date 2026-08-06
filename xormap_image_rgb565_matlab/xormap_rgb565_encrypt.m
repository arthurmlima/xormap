function [cipher, seed] = xormap_rgb565_encrypt(plain_packed, key_bits)
%XORMAP_RGB565_ENCRYPT XOR-stream-cipher a packed RGB565 image with xormap.
%   [CIPHER, SEED] = XORMAP_RGB565_ENCRYPT(PLAIN_PACKED, KEY_BITS) is the
%   16-bit/pixel analogue of xormap_image_matlab's xormap_image_encrypt.
%   PLAIN_PACKED must be a 2-D uint16 RGB565 image (see
%   rgb888_to_rgb565.m). Derives a plaintext-related K-bit initial
%   condition (K = numel(KEY_BITS), any K > 4 -- K need not be a multiple
%   of 8 or 16: xormap_keystream_words.m concatenates K-bit iterations
%   into one flat bit stream and slices an arbitrary-length prefix off
%   it, so K and the word grid never need to align)
%       SEED = KEY_BITS XOR HASH_EXPAND_BITS(bytes-of(PLAIN_PACKED), K)
%   generates keystream by iterating xormap_transform from SEED
%   (xormap_keystream_words.m: K/16 pixels' worth of keystream per
%   iteration on average), and XORs it with PLAIN_PACKED.

    k = numel(key_bits);
    if k <= 4
        error('xormap_rgb565_encrypt:badKeyLength', 'key_bits length (K) must be > 4');
    end
    if ~ismatrix(plain_packed) || ~isa(plain_packed, 'uint16')
        error('xormap_rgb565_encrypt:badImage', 'plain_packed must be a 2-D uint16 RGB565 image');
    end

    [rows, cols] = size(plain_packed);
    plain_words = reshape(plain_packed.', 1, rows * cols);  % row-major flatten

    hash_bits = hash_expand_bits(words_to_bytes_be(plain_words), k);
    seed = xor(key_bits, hash_bits);

    keystream_words = uint16(xormap_keystream_words(seed, 16, numel(plain_words)));
    cipher_words = bitxor(plain_words, keystream_words);

    cipher = reshape(cipher_words, cols, rows).';
end
