function [cipher, seed] = xormap_image_encrypt(plain, key_bits)
%XORMAP_IMAGE_ENCRYPT XOR-stream-cipher a grayscale image with xormap.
%   [CIPHER, SEED] = XORMAP_IMAGE_ENCRYPT(PLAIN, KEY_BITS) derives a
%   plaintext-related K-bit initial condition (K = numel(KEY_BITS), any
%   K > 4 -- K need not be a multiple of 8: xormap_keystream.m
%   concatenates K-bit iterations into one flat bit stream and slices an
%   arbitrary-length prefix off it, so K and the byte grid never need to
%   align)
%       SEED = KEY_BITS XOR HASH_EXPAND_BITS(PLAIN, K)
%   generates keystream by iterating xormap_transform from SEED
%   (xormap_keystream.m: K/8 pixels' worth of keystream per iteration on
%   average), and XORs it with PLAIN. PLAIN must be a 2-D uint8 matrix.
%
%   Being plaintext-related, a single-bit change anywhere in PLAIN gives a
%   completely different SEED (hash avalanche) and therefore a completely
%   different keystream -- this is what npcr_uaci.m measures.

    k = numel(key_bits);
    if k <= 4
        error('xormap_image_encrypt:badKeyLength', 'key_bits length (K) must be > 4');
    end
    if ~ismatrix(plain) || ~isa(plain, 'uint8')
        error('xormap_image_encrypt:badImage', 'plain must be a 2-D uint8 matrix (grayscale)');
    end

    [rows, cols] = size(plain);
    plain_bytes = reshape(plain.', 1, rows * cols);  % row-major flatten

    hash_bits = hash_expand_bits(plain_bytes, k);
    seed = xor(key_bits, hash_bits);

    keystream_bytes = xormap_keystream(seed, numel(plain_bytes));
    cipher_bytes = bitxor(plain_bytes, keystream_bytes);

    cipher = reshape(cipher_bytes, cols, rows).';
end
