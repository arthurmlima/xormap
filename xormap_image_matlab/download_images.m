% download_images.m
% Downloads the grayscale test images used by run_all.m from the USC-SIPI
% Miscellaneous volume (https://sipi.usc.edu/database/). Each image is
% served as an uncompressed 8-bit grayscale TIFF via download.php.
%
% Note: SIPI's Peppers image (4.2.07) is only distributed in color, so
% 7.1.01 (Truck, 512x512) is used in its place -- every test image here
% is natively grayscale, none are RGB converted.

this_dir = fileparts(mfilename('fullpath'));
img_dir = fullfile(this_dir, 'images');
if ~exist(img_dir, 'dir')
    mkdir(img_dir);
end

names = {'boat.512', '5.1.09', '7.1.01'};

for i = 1:numel(names)
    name = names{i};
    url = sprintf('https://sipi.usc.edu/database/download.php?vol=misc&img=%s', name);
    dest = fullfile(img_dir, [name '.tiff']);
    fprintf('Downloading %s -> %s\n', name, dest);
    websave(dest, url);
end

fprintf('Done.\n');
