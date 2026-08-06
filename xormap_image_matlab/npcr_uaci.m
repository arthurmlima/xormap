function [npcr, uaci] = npcr_uaci(img1, img2, max_value)
%NPCR_UACI Number of Pixels Change Rate and Unified Average Changing
%   Intensity between two same-size images, as percentages.
%   IMG1/IMG2 are expected to be the two cipher images produced by
%   encrypting a plain image and a 1-bit-perturbed copy of it.
%
%   MAX_VALUE is the maximum representable value of one element (default
%   255, for 8-bit images; use e.g. 31 or 63 for a 5- or 6-bit RGB565
%   channel, or 65535 for a full packed 16-bit RGB565 word). Ideal values
%   for a good cipher are NPCR ~= 99.6094%% and UACI ~= 33.4635%%
%   regardless of MAX_VALUE (both are already normalized).

    if nargin < 3
        max_value = 255;
    end

    d = img1 ~= img2;
    npcr = 100 * sum(d(:)) / numel(d);
    uaci = 100 * mean(abs(double(img1(:)) - double(img2(:)))) / max_value;
end
