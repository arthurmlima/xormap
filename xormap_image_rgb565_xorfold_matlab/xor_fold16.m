function [word, folded_bits] = xor_fold16(state_bits)
%XOR_FOLD16 XOR-fold a K-bit state into one 16-bit word.
%   WORD = XOR_FOLD16(STATE_BITS) folds consecutive 16-bit chunks:
%
%       WORD bit b = xor(STATE_BITS(b:16:end)), b = 1,...,16.
%
%   STATE_BITS uses xormap's convention that element 1 is the LSB.  K
%   must be a multiple of 16.  Every input bit contributes exactly once.
%   The optional FOLDED_BITS output is the 1x16 LSB-first logical vector.

    if ~isvector(state_bits) || isempty(state_bits) || mod(numel(state_bits), 16) ~= 0
        error('xor_fold16:badLength', ...
            'state_bits must be a nonempty vector whose length is a multiple of 16');
    end
    if ~islogical(state_bits) && any(state_bits(:) ~= 0 & state_bits(:) ~= 1)
        error('xor_fold16:nonBinary', 'state_bits values must be binary');
    end

    state_bits = logical(state_bits(:)).';
    chunks = reshape(state_bits, 16, []);
    folded_bits = (mod(sum(chunks, 2), 2) ~= 0).';
    word = uint16(sum(double(folded_bits) .* 2 .^ (0:15)));
end
