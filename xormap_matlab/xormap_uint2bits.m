function bits = xormap_uint2bits(value, k)
%XORMAP_UINT2BITS Convert a nonnegative integer into a K-bit vector.
%   BITS = XORMAP_UINT2BITS(VALUE, K) returns a 1xK logical row vector,
%   bit 1 = LSB, matching the bit order xormap_transform expects.
%   VALUE must satisfy 0 <= VALUE < 2^K, and K <= 53 (double precision
%   integer range). For wider K, build the bit vector directly instead.

    if k > 53
        error('xormap_uint2bits:tooWide', ...
            'K > 53 cannot be represented exactly as a double; construct the bit vector directly');
    end
    if value < 0 || value >= 2^k
        error('xormap_uint2bits:outOfRange', 'value must satisfy 0 <= value < 2^K');
    end

    bits = false(1, k);
    for b = 0:(k - 1)
        bits(b + 1) = bitget(value, b + 1) ~= 0;
    end
end
