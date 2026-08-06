function bytes = words_to_bytes_be(words)
%WORDS_TO_BYTES_BE Expand a uint16 word vector into big-endian bytes
%   (hi, lo, hi, lo, ...). Used only to build a deterministic byte vector
%   to feed into HASH_EXPAND_BITS for seed derivation -- the byte order
%   convention doesn't need to match any external spec, only be consistent
%   between encrypt calls, since nothing outside this pipeline reads it.

    hi = uint8(bitshift(words, -8));
    lo = uint8(bitand(words, 255));
    bytes = reshape([hi; lo], 1, []);
end
