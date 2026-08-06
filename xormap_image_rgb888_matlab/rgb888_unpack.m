function rgb = rgb888_unpack(packed)
%RGB888_UNPACK Inverse of RGB888_PACK: packed HxW uint32 -> HxWx3 uint8.

    R = uint8(bitshift(packed, -16));
    G = uint8(bitand(bitshift(packed, -8), 255));
    B = uint8(bitand(packed, 255));

    rgb = cat(3, R, G, B);
end
