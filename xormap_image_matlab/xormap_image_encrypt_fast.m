function [cipher, seed, iterations] = xormap_image_encrypt_fast(plain, key_bits)
%XORMAP_IMAGE_ENCRYPT_FAST XORMAP_IMAGE_ENCRYPT on the fast transform.
%   [CIPHER, SEED, ITERATIONS] = XORMAP_IMAGE_ENCRYPT_FAST(PLAIN, KEY_BITS)
%   is bit-for-bit identical to XORMAP_IMAGE_ENCRYPT -- same
%   plaintext-related seed, same keystream, same ciphertext -- but uses
%   XORMAP_KEYSTREAM_FAST so the all-image K sweep is tractable.
%   test_xormap_gray_fast.m asserts the two agree exactly.
%
%   ITERATIONS is the number of xormap register advances the encryption
%   consumed, ceil(numel(PLAIN) * 8 / K). Reported so the sweep can record
%   the cost side of the K trade-off: a wider K means proportionally
%   FEWER iterations, since each one yields K/8 pixels.

    k = numel(key_bits);
    if k <= 4
        error('xormap_image_encrypt_fast:badKeyLength', 'key_bits length (K) must be > 4');
    end
    if ~ismatrix(plain) || ~isa(plain, 'uint8')
        error('xormap_image_encrypt_fast:badImage', ...
            'plain must be a 2-D uint8 matrix (grayscale)');
    end

    [rows, cols] = size(plain);
    plain_bytes = reshape(plain.', 1, rows * cols);  % row-major flatten

    hash_bits = hash_expand_bits(plain_bytes, k);
    seed = xor(logical(key_bits(:)).', hash_bits);

    keystream_bytes = xormap_keystream_fast(seed, numel(plain_bytes));
    cipher_bytes = bitxor(plain_bytes, keystream_bytes);

    cipher = reshape(cipher_bytes, cols, rows).';
    iterations = ceil(numel(plain_bytes) * 8 / k);
end
