function bits = hash_expand_bits(byte_vector, k)
%HASH_EXPAND_BITS Expand SHA-256(byte_vector) to exactly K bits.
%   A simple counter-mode hash KDF (same idea as HKDF-Expand): concatenates
%   SHA256([byte_vector, counter]) for counter = 0, 1, 2, ... until there
%   are at least K bits, then truncates. For K <= 256 this just truncates
%   a single SHA-256 digest; a single counter byte is enough for every K
%   used in this project (K <= 512 needs at most 2 blocks).
%
%   This is what lets xormap_image_encrypt/decrypt use any K > 4, instead
%   of being pinned to K=256 (SHA-256's own width).

    block_bits = 256;
    num_blocks = ceil(k / block_bits);

    bits = false(1, num_blocks * block_bits);
    for c = 0:(num_blocks - 1)
        block = sha256_bits([byte_vector(:).', uint8(c)]);
        bits(c * block_bits + 1 : (c + 1) * block_bits) = block;
    end

    bits = bits(1:k);
end
