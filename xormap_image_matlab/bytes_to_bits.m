function bits = bytes_to_bits(bytes)
%BYTES_TO_BITS Expand a uint8 row vector into a 1x(8*N) logical bit vector.
%   Bit 1 = LSB of bytes(1); bytes are expanded in order, LSB-first within
%   each byte -- the same convention xormap_uint2bits uses, so a byte
%   vector and the bit vector it expands to agree on which end is which.

    n = numel(bytes);
    bits = false(1, 8 * n);
    for idx = 1:n
        base = (idx - 1) * 8;
        for b = 0:7
            bits(base + b + 1) = bitget(bytes(idx), b + 1) ~= 0;
        end
    end
end
