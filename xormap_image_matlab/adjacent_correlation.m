function [r, x, y] = adjacent_correlation(img, direction, num_samples)
%ADJACENT_CORRELATION Pearson correlation coefficient between NUM_SAMPLES
%   random adjacent-pixel pairs of IMG. DIRECTION is 'horizontal',
%   'vertical', or 'diagonal'. Standard image-encryption benchmark: near
%   1 for a plain image (neighbouring pixels are similar), near 0 for a
%   well-scrambled cipher image.
%
%   [R, X, Y] = ... also returns the sampled pixel pairs, e.g. for a
%   scatter plot.

    img = double(img);
    [rows, cols] = size(img);

    switch direction
        case 'horizontal'
            r_idx = randi(rows, num_samples, 1);
            c_idx = randi(cols - 1, num_samples, 1);
            x = img(sub2ind(size(img), r_idx, c_idx));
            y = img(sub2ind(size(img), r_idx, c_idx + 1));
        case 'vertical'
            r_idx = randi(rows - 1, num_samples, 1);
            c_idx = randi(cols, num_samples, 1);
            x = img(sub2ind(size(img), r_idx, c_idx));
            y = img(sub2ind(size(img), r_idx + 1, c_idx));
        case 'diagonal'
            r_idx = randi(rows - 1, num_samples, 1);
            c_idx = randi(cols - 1, num_samples, 1);
            x = img(sub2ind(size(img), r_idx, c_idx));
            y = img(sub2ind(size(img), r_idx + 1, c_idx + 1));
        otherwise
            error('adjacent_correlation:badDirection', 'unknown direction %s', direction);
    end

    R = corrcoef(x, y);
    r = R(1, 2);
end
