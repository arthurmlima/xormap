function rgb = rgb565_to_rgb888_preview(packed)
%RGB565_TO_RGB888_PREVIEW Expand a packed RGB565 image back to an HxWx3
%   uint8 truecolor image for display, via standard bit replication (so 0
%   stays 0 and the max value stays 255): R8 = (R5<<3)|(R5>>2), etc. This
%   is a display convenience only -- the cipher itself operates on the
%   packed 16-bit words, never on this expanded form.

    [R5, G6, B5] = rgb565_to_channels(packed);
    R8 = bitor(bitshift(R5, 3), bitshift(R5, -2));
    G8 = bitor(bitshift(G6, 2), bitshift(G6, -4));
    B8 = bitor(bitshift(B5, 3), bitshift(B5, -2));
    rgb = cat(3, R8, G8, B8);
end
