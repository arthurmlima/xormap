function e = shannon_entropy(img, nbits)
%SHANNON_ENTROPY Information entropy of an image, in bits.
%   E = SHANNON_ENTROPY(IMG) treats IMG as 8-bit (256 levels): ideal for a
%   well-encrypted 8-bit image is 8.0 (uniform histogram); a plain
%   photographic image is typically well below that.
%
%   E = SHANNON_ENTROPY(IMG, NBITS) uses 2^NBITS levels instead, e.g. for
%   an RGB565 channel (NBITS=5 for R/B, NBITS=6 for G; ideal cipher
%   entropy is then 5.0 / 6.0 rather than 8.0).

    if nargin < 2
        nbits = 8;
    end

    counts = histcounts(double(img(:)), -0.5:1:(2^nbits - 0.5));
    p = counts / sum(counts);
    p = p(p > 0);
    e = -sum(p .* log2(p));
end
