function bytes = xormap_keystream(seed_bits, num_bytes)
%XORMAP_KEYSTREAM Generate NUM_BYTES of keystream from SEED_BITS.
%   Iterates xormap_transform (../xormap_matlab) starting at SEED_BITS
%   exactly the way Genv_xormap.m's `en` pulse advances the hardware
%   register (x_reg <= xormap_transform(x_reg)), appending each K-bit
%   output to the stream, then truncates to NUM_BYTES*8 bits and packs
%   them into bytes.

    k = numel(seed_bits);
    num_bits_needed = num_bytes * 8;
    num_iters = ceil(num_bits_needed / k);

    stream = false(1, num_iters * k);
    v = seed_bits;
    for it = 1:num_iters
        v = xormap_transform(v, k);
        stream((it - 1) * k + 1 : it * k) = v;
    end

    bytes = bits_to_bytes(stream(1:num_bits_needed));
end
