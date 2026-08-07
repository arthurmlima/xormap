function bytes = bits_to_bytes(bits)
%BITS_TO_BYTES Inverse of BYTES_TO_BITS. numel(bits) must be a multiple of 8.
%   Bit 1 of each group of 8 is that byte's LSB.
%
%   Vectorised: each byte is a weighted sum of its 8 bits, so the whole
%   stream is one 1x8 * 8xN matrix product. The previous per-bit loop with
%   a bitset call ran ~8 interpreted operations per byte, which dominated
%   the database-wide sweeps (they pack tens of millions of bytes per K).

    if mod(numel(bits), 8) ~= 0
        error('bits_to_bytes:badLength', 'numel(bits) must be a multiple of 8');
    end

    grouped = reshape(logical(bits(:)), 8, []);      % column j = byte j, LSB first
    bytes = uint8((2 .^ (0:7)) * grouped);           % 1x8 * 8xN -> 1xN
end
