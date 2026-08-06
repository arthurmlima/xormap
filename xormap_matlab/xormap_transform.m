function result = xormap_transform(input, k)
%XORMAP_TRANSFORM Compute the K-bit result produced by Genv_xormap.m.
%   RESULT = XORMAP_TRANSFORM(INPUT, K) is a direct port of
%   xormap::transform in xormap_cpp/xormap.cpp: a closed-form
%   re-derivation of the pairwise-XOR map that Genv_xormap.m builds by
%   constructing chunks (demo_chunks.m), windowing them
%   (extract_3section.m) and reversing twice (the two reversals cancel,
%   so output bit b uses source column start_col + b).
%
%   INPUT and RESULT are 1xK logical row vectors, bit 1 = LSB (INPUT(1)
%   is bit 0, INPUT(K) is bit K-1). Using a bit vector instead of a
%   fixed-width integer type means this works for any K, the same way
%   xormap::Integer (boost::multiprecision::cpp_int) gives the C++
%   version arbitrary width.
%
%   K must be greater than 4.
%
%   See also XORMAP_UINT2BITS, XORMAP_BITS2UINT.

    if k <= 4
        error('xormap_transform:invalidK', 'K must be greater than 4');
    end
    if ~isvector(input) || numel(input) ~= k
        error('xormap_transform:invalidInput', ...
            'input must be a 1x%d bit vector', k);
    end

    input = logical(input(:)).';  % row vector, bit 1 = LSB
    result = false(1, k);

    % MATLAB, one-based column index.
    start_col = (k - 1) - floor(k / 2);

    for output_bit = 0:(k - 1)
        column = start_col + output_bit;

        if column <= k - 1
            i = k - column;
            j = k;
        else
            i = 1;
            j = 2 * k - 1 - column;
        end

        value = false;
        while i < j
            value = xor(value, input(i));
            value = xor(value, input(j));
            i = i + 1;
            j = j - 1;
        end

        result(output_bit + 1) = value;
    end
end
