% download_images.m
% Downloads the color test images used by run_all_rgb565.m from the
% USC-SIPI Miscellaneous volume (https://sipi.usc.edu/database/), served
% as uncompressed 24-bit truecolor TIFFs via download.php. SIPI does not
% host RGB565 images directly (nobody does -- it's a packed hardware pixel
% format, not a distribution format), so rgb888_to_rgb565.m converts these
% down to 16-bit 5-6-5 after loading.
%
% Picked to match xormap_image_matlab's grayscale set in spirit: object
% images, not the personal-photo NTSC test stills (4.1.01-4.1.04 etc).

this_dir = fileparts(mfilename('fullpath'));
img_dir = fullfile(this_dir, 'images');
if ~exist(img_dir, 'dir')
    mkdir(img_dir);
end

names = {'4.2.03', '4.2.05', '4.2.07'};  % Mandrill, Airplane (F-16), Peppers -- all 512x512 color

for i = 1:numel(names)
    name = names{i};
    url = sprintf('https://sipi.usc.edu/database/download.php?vol=misc&img=%s', name);
    dest = fullfile(img_dir, [name '.tiff']);
    fprintf('Downloading %s -> %s\n', name, dest);
    websave(dest, url);
end

fprintf('Done.\n');
