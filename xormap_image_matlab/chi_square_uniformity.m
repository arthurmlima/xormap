function [chi2, dof, critical, passes] = chi_square_uniformity(img, nbits, alpha)
%CHI_SQUARE_UNIFORMITY Chi-square test of histogram uniformity.
%   [CHI2, DOF, CRITICAL, PASSES] = CHI_SQUARE_UNIFORMITY(IMG) tests the
%   null hypothesis "the 8-bit histogram of IMG is uniform", the standard
%   quantitative companion to eyeballing a cipher histogram:
%
%       chi2 = sum_i (observed_i - expected_i)^2 / expected_i
%       expected_i = numel(IMG) / 2^NBITS   (flat)
%
%   A well-encrypted image has a flat histogram, so it should FAIL to be
%   rejected -- CHI2 below CRITICAL, PASSES true. A plain photographic
%   image has a wildly non-uniform histogram and gives an enormous CHI2.
%
%   NBITS defaults to 8 (256 bins, DOF = 255). ALPHA defaults to 0.05.
%
%   CRITICAL is the upper-tail chi-square critical value at ALPHA with DOF
%   degrees of freedom. It is computed via the Wilson-Hilferty cube-root
%   normal approximation rather than chi2inv, so this needs no Statistics
%   Toolbox; at DOF = 255 that approximation is accurate to ~0.1%, far
%   inside the margin that matters here (cipher images land in the low
%   hundreds, plain images in the millions).
%
%   Note PASSES is "not rejected at ALPHA", not proof of uniformity: with
%   a large enough image even a tiny real bias is detectable, and a single
%   test at alpha=0.05 rejects 5% of genuinely uniform images by chance.

    if nargin < 2 || isempty(nbits)
        nbits = 8;
    end
    if nargin < 3 || isempty(alpha)
        alpha = 0.05;
    end

    levels = 2 ^ nbits;
    counts = histcounts(double(img(:)), -0.5:1:(levels - 0.5));
    expected = numel(img) / levels;

    chi2 = sum((counts - expected) .^ 2) / expected;
    dof = levels - 1;
    critical = chi2_critical(dof, alpha);
    passes = chi2 <= critical;
end

function c = chi2_critical(dof, alpha)
% Wilson-Hilferty: (X/dof)^(1/3) is approximately normal with
% mean 1 - 2/(9*dof) and variance 2/(9*dof).
    z = norm_upper_quantile(alpha);
    t = 2 / (9 * dof);
    c = dof * (1 - t + z * sqrt(t)) ^ 3;
end

function z = norm_upper_quantile(alpha)
% Upper-tail standard-normal quantile z with P(Z > z) = alpha, via the
% Beasley-Springer-Moro style rational approximation to the inverse normal
% CDF (Acklam's coefficients). Accurate to ~1e-9 over the useful range.
    p = 1 - alpha;

    a = [-3.969683028665376e+01,  2.209460984245205e+02, ...
         -2.759285104469687e+02,  1.383577518672690e+02, ...
         -3.066479806614716e+01,  2.506628277459239e+00];
    b = [-5.447609879822406e+01,  1.615858368580409e+02, ...
         -1.556989798598866e+02,  6.680131188771972e+01, ...
         -1.328068155288572e+01];
    c = [-7.784894002430293e-03, -3.223964580411365e-01, ...
         -2.400758277161838e+00, -2.549732539343734e+00, ...
          4.374664141464968e+00,  2.938163982698783e+00];
    d = [ 7.784695709041462e-03,  3.224671290700398e-01, ...
          2.445134137142996e+00,  3.754408661907416e+00];

    p_low = 0.02425;
    if p < p_low
        q = sqrt(-2 * log(p));
        z = (((((c(1)*q + c(2))*q + c(3))*q + c(4))*q + c(5))*q + c(6)) / ...
            ((((d(1)*q + d(2))*q + d(3))*q + d(4))*q + 1);
    elseif p <= 1 - p_low
        q = p - 0.5;
        r = q * q;
        z = (((((a(1)*r + a(2))*r + a(3))*r + a(4))*r + a(5))*r + a(6)) * q / ...
            (((((b(1)*r + b(2))*r + b(3))*r + b(4))*r + b(5))*r + 1);
    else
        q = sqrt(-2 * log(1 - p));
        z = -(((((c(1)*q + c(2))*q + c(3))*q + c(4))*q + c(5))*q + c(6)) / ...
             ((((d(1)*q + d(2))*q + d(3))*q + d(4))*q + 1);
    end
end
