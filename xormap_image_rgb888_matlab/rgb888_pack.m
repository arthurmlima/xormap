function packed = rgb888_pack(rgb)
%RGB888_PACK Pack an HxWx3 uint8 truecolor image into a packed HxW uint32
%   24-bit-per-pixel image: bits[23:16]=R, [15:8]=G, [7:0]=B. Unlike
%   RGB565 this is lossless -- SIPI's color images are already native
%   24-bit truecolor, so no channel truncation is needed.

    if ndims(rgb) ~= 3 || size(rgb, 3) ~= 3 || ~isa(rgb, 'uint8')
        error('rgb888_pack:badImage', 'rgb must be an HxWx3 uint8 image');
    end

    R = uint32(rgb(:, :, 1));
    G = uint32(rgb(:, :, 2));
    B = uint32(rgb(:, :, 3));

    packed = bitshift(R, 16) + bitshift(G, 8) + B;
end
