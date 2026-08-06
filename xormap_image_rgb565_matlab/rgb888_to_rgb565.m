function packed = rgb888_to_rgb565(rgb)
%RGB888_TO_RGB565 Convert an HxWx3 uint8 truecolor image to a packed HxW
%   uint16 RGB565 image: bits [15:11]=R5, [10:5]=G6, [4:0]=B5 -- the
%   standard layout used by ST7789-class TFT controllers (matches this
%   project's tangprimer25k_st7789_top target). Each channel is simply
%   truncated to its higher bits (R,B: 8->5; G: 8->6), not rounded.

    if ndims(rgb) ~= 3 || size(rgb, 3) ~= 3 || ~isa(rgb, 'uint8')
        error('rgb888_to_rgb565:badImage', 'rgb must be an HxWx3 uint8 image');
    end

    R5 = bitshift(uint16(rgb(:, :, 1)), -3);
    G6 = bitshift(uint16(rgb(:, :, 2)), -2);
    B5 = bitshift(uint16(rgb(:, :, 3)), -3);

    packed = bitshift(R5, 11) + bitshift(G6, 5) + B5;
end
