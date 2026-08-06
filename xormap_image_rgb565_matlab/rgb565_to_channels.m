function [R5, G6, B5] = rgb565_to_channels(packed)
%RGB565_TO_CHANNELS Split a packed uint16 RGB565 image into its three
%   component matrices (as uint8): R5/B5 in [0,31], G6 in [0,63].

    R5 = uint8(bitshift(packed, -11));
    G6 = uint8(bitand(bitshift(packed, -5), 63));
    B5 = uint8(bitand(packed, 31));
end
