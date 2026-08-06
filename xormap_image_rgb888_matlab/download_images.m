% download_images.m
% Downloads every color image currently distributed by the USC-SIPI
% database (https://sipi.usc.edu/database/) into images/. Textures and
% Sequences are entirely grayscale/mono, so only Miscellaneous and
% Aerials contribute:
%
%   misc    - 14 color images (4.1.01-08, 4.2.01/03/05/06/07, house)
%   aerials - 37 color images (2.1.01-12 @512, 2.2.01-24 @1024, wash-ir @2250)
%
% (Lena/4.2.04 and Tiffany/4.2.02 are no longer distributed by SIPI, so
% they're excluded -- see xormap_image_matlab's README for the same note
% on the grayscale side.)
%
% All images are already native 24-bit truecolor (8 bits/channel), unlike
% RGB565 which required down-conversion -- see rgb888_pack.m.
%
% Writes images/manifest.csv recording which volume each file came from.

this_dir = fileparts(mfilename('fullpath'));
img_dir = fullfile(this_dir, 'images');
if ~exist(img_dir, 'dir')
    mkdir(img_dir);
end

misc_names = {'4.1.01', '4.1.02', '4.1.03', '4.1.04', '4.1.05', '4.1.06', ...
              '4.1.07', '4.1.08', '4.2.01', '4.2.03', '4.2.05', '4.2.06', ...
              '4.2.07', 'house'};

aerials_names = [ ...
    arrayfun(@(n) sprintf('2.1.%02d', n), 1:12, 'UniformOutput', false), ...
    arrayfun(@(n) sprintf('2.2.%02d', n), 1:24, 'UniformOutput', false), ...
    {'wash-ir'}];

manifest_path = fullfile(img_dir, 'manifest.csv');
mfid = fopen(manifest_path, 'w');
fprintf(mfid, 'filename,volume,name\n');

all_names = [misc_names, aerials_names];
all_vols = [repmat({'misc'}, 1, numel(misc_names)), repmat({'aerials'}, 1, numel(aerials_names))];

fprintf('Downloading %d color images (%d misc + %d aerials)...\n', ...
    numel(all_names), numel(misc_names), numel(aerials_names));

for i = 1:numel(all_names)
    name = all_names{i};
    vol = all_vols{i};
    dest = fullfile(img_dir, [name '.tiff']);
    if exist(dest, 'file')
        fprintf('[%3d/%3d] %-10s (%s) already present, skipping\n', i, numel(all_names), name, vol);
    else
        url = sprintf('https://sipi.usc.edu/database/download.php?vol=%s&img=%s', vol, name);
        fprintf('[%3d/%3d] %-10s (%s) downloading...\n', i, numel(all_names), name, vol);
        websave(dest, url);
    end
    fprintf(mfid, '%s,%s,%s\n', [name '.tiff'], vol, name);
end

fclose(mfid);
fprintf('Done. Wrote %s\n', manifest_path);
