function [npcr_ideal, uaci_ideal] = npcr_uaci_ideal(nbits)
%NPCR_UACI_IDEAL Theoretical NPCR/UACI (percent) for two independent
%   uniformly-random images with 2^NBITS levels per element (Wu, Noonan &
%   Agaian 2011). NPCR_UACI.m's default (nbits=8, i.e. L=256) gives the
%   commonly-quoted 99.6094%% / 33.4635%%; other bit depths (e.g. RGB565's
%   5/6/5 channels, or its packed 16-bit word) need their own ideal, since
%   both depend on L = 2^NBITS:
%       NPCR_ideal = 100*(L-1)/L
%       UACI_ideal = 100*(L+1)/(3*L)

    L = 2^nbits;
    npcr_ideal = 100 * (L - 1) / L;
    uaci_ideal = 100 * (L + 1) / (3 * L);
end
