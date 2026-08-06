function value = xormap_bits2uint(bits)
%XORMAP_BITS2UINT Convert a bit vector back into a nonnegative integer.
%   VALUE = XORMAP_BITS2UINT(BITS) is the inverse of XORMAP_UINT2BITS:
%   bit 1 = LSB. Limited to vectors of length <= 53 (double precision
%   integer range); for wider results, use the bit vector directly.

    k = numel(bits);
    if k > 53
        error('xormap_bits2uint:tooWide', ...
            'K > 53 cannot be represented exactly as a double; use the bit vector directly');
    end

    value = 0;
    for b = 0:(k - 1)
        if bits(b + 1)
            value = value + 2^b;
        end
    end
end
