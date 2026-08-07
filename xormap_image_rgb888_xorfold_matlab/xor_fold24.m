function [word, folded_bits] = xor_fold24(state_bits)
%XOR_FOLD24 XOR-fold a K-bit state into one 24-bit value.
%   WORD = XOR_FOLD24(STATE_BITS) folds consecutive 24-bit chunks:
%
%       WORD bit b = xor(STATE_BITS(b:24:end)), b = 1,...,24.
%
%   STATE_BITS uses xormap's convention that element 1 is the LSB. K must
%   be a multiple of 24. Every input bit contributes exactly once. WORD is
%   returned as uint32 (only the low 24 bits are meaningful). The optional
%   FOLDED_BITS output is the 1x24 LSB-first logical vector.
%
%   24-bit analogue of the sibling RGB565 project's xor_fold16.m.

    if ~isvector(state_bits) || isempty(state_bits) || mod(numel(state_bits), 24) ~= 0
        error('xor_fold24:badLength', ...
            'state_bits must be a nonempty vector whose length is a multiple of 24');
    end
    if ~islogical(state_bits) && any(state_bits(:) ~= 0 & state_bits(:) ~= 1)
        error('xor_fold24:nonBinary', 'state_bits values must be binary');
    end

    state_bits = logical(state_bits(:)).';
    chunks = reshape(state_bits, 24, []);
    folded_bits = (mod(sum(chunks, 2), 2) ~= 0).';
    word = uint32(sum(double(folded_bits) .* 2 .^ (0:23)));
end
