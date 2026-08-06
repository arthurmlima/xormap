function bytes = bits_to_bytes(bits)
%BITS_TO_BYTES Inverse of BYTES_TO_BITS. numel(bits) must be a multiple of 8.

    if mod(numel(bits), 8) ~= 0
        error('bits_to_bytes:badLength', 'numel(bits) must be a multiple of 8');
    end

    n = numel(bits) / 8;
    bytes = zeros(1, n, 'uint8');
    for idx = 1:n
        base = (idx - 1) * 8;
        v = uint8(0);
        for b = 0:7
            if bits(base + b + 1)
                v = bitset(v, b + 1);
            end
        end
        bytes(idx) = v;
    end
end
