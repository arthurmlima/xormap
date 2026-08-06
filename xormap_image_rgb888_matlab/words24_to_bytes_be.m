function bytes = words24_to_bytes_be(words)
%WORDS24_TO_BYTES_BE Expand a uint32 word vector (only the low 24 bits of
%   each element meaningful) into big-endian byte triples (hi, mid, lo,
%   hi, mid, lo, ...). Used only to build a deterministic byte vector to
%   feed into HASH_EXPAND_BITS for seed derivation -- the byte order
%   convention doesn't need to match any external spec, only be
%   consistent between encrypt calls.

    hi = uint8(bitshift(words, -16));
    mid = uint8(bitand(bitshift(words, -8), 255));
    lo = uint8(bitand(words, 255));
    bytes = reshape([hi; mid; lo], 1, []);
end
