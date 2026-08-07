function [psnr, mse] = psnr_db(a, b, max_value)
%PSNR_DB Peak signal-to-noise ratio between two same-size images, in dB.
%   [PSNR, MSE] = PSNR_DB(A, B) treats the data as 8-bit (peak 255):
%
%       MSE  = mean((A - B).^2)
%       PSNR = 10 * log10(max_value^2 / MSE)
%
%   PSNR_DB(A, B, MAX_VALUE) overrides the peak (e.g. 65535).
%
%   HOW TO READ IT FOR A CIPHER. PSNR is a fidelity measure, so its role
%   here is inverted from the compression/watermarking setting where a
%   HIGH value is the goal:
%
%     * PSNR(plain, cipher) -- encryption quality. LOW is good; a cipher
%       that looks like uniform noise relative to the plaintext lands
%       around 8-9 dB. A high value would mean the ciphertext still
%       resembles the plaintext.
%
%     * PSNR(plain, decrypted-with-the-right-key) -- round-trip fidelity.
%       For this cipher it is exactly +Inf (MSE = 0), because the scheme
%       is a pure XOR stream cipher: it is lossless by construction, with
%       no quantisation step to lose anything. Worth asserting as a
%       correctness check, but it carries no tuning information -- it can
%       only ever be Inf or a bug.
%
%     * PSNR(plain, decrypted-with-a-wrong-key) -- key sensitivity in dB.
%       Should be as low as PSNR(plain, cipher).
%
%   Returns Inf when the two images are identical, which is the correct
%   limit rather than a division-by-zero error.

    if nargin < 3 || isempty(max_value)
        max_value = 255;
    end

    a = double(a(:));
    b = double(b(:));
    if numel(a) ~= numel(b)
        error('psnr_db:sizeMismatch', 'A and B must have the same number of elements');
    end

    mse = mean((a - b) .^ 2);
    if mse == 0
        psnr = Inf;
    else
        psnr = 10 * log10(max_value ^ 2 / mse);
    end
end
