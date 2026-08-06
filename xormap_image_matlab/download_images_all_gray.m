% download_images_all_gray.m
% Downloads EVERY grayscale image currently distributed by the USC-SIPI
% database (https://sipi.usc.edu/database/) into images/, the grayscale
% counterpart of xormap_image_rgb888_matlab/download_images.m (which does
% the same for the 51 color images).
%
% All four SIPI volumes contribute, per each volume's own Gray/Mono column:
%
%   misc      -  25 gray (5.1.x, 5.2.x, 5.3.x, 7.1.x, 7.2.01, boat.512,
%                gray21.512, ruler.512)
%   aerials   -   1 gray (3.2.25) -- the other 37 aerials are color
%   textures  -  64 mono (Brodatz 1.1.x-1.5.x plus the texmos mosaics)
%   sequences -  69 gray (6.1.x, 6.2.x, 6.3.x, motion01-10.512)
%
% = 159 images, ~51.7M pixels. SIPI no longer distributes elaine.512,
% numbers.512 or testpat.1k (nor 4.2.04/lena and 4.2.02/tiffany on the
% color side), so the volume pages' nominal "28 monochrome" for misc is 25
% in practice -- the same withdrawal note the RGB888 sibling carries.
%
% Writes images/manifest_gray.csv with the volume, pixel dimensions and
% channel count of every file that downloaded as true single-channel 8-bit
% grayscale. Anything that arrives multi-channel is reported and excluded
% rather than silently converted, so the sweep only ever sees native
% grayscale (the same "no RGB-converted images" rule as the 3-image
% download_images.m).
%
% Re-running skips files already on disk, so an interrupted download can
% simply be run again.

this_dir = fileparts(mfilename('fullpath'));
img_dir = fullfile(this_dir, 'images');
if ~exist(img_dir, 'dir')
    mkdir(img_dir);
end

misc_names = { ...
    '5.1.09', '5.1.10', '5.1.11', '5.1.12', '5.1.13', '5.1.14', ...
    '5.2.08', '5.2.09', '5.2.10', '5.3.01', '5.3.02', '7.1.01', ...
    '7.1.02', '7.1.03', '7.1.04', '7.1.05', '7.1.06', '7.1.07', ...
    '7.1.08', '7.1.09', '7.1.10', '7.2.01', 'boat.512', 'gray21.512', ...
    'ruler.512'};

aerials_names = {'3.2.25'};

textures_names = { ...
    '1.1.01', '1.1.02', '1.1.03', '1.1.04', '1.1.05', '1.1.06', ...
    '1.1.07', '1.1.08', '1.1.09', '1.1.10', '1.1.11', '1.1.12', ...
    '1.1.13', '1.2.01', '1.2.02', '1.2.03', '1.2.04', '1.2.05', ...
    '1.2.06', '1.2.07', '1.2.08', '1.2.09', '1.2.10', '1.2.11', ...
    '1.2.12', '1.2.13', '1.3.01', '1.3.02', '1.3.03', '1.3.04', ...
    '1.3.05', '1.3.06', '1.3.07', '1.3.08', '1.3.09', '1.3.10', ...
    '1.3.11', '1.3.12', '1.3.13', '1.4.01', '1.4.02', '1.4.03', ...
    '1.4.04', '1.4.05', '1.4.06', '1.4.07', '1.4.08', '1.4.09', ...
    '1.4.10', '1.4.11', '1.4.12', '1.5.01', '1.5.02', '1.5.03', ...
    '1.5.04', '1.5.05', '1.5.06', '1.5.07', 'texmos1.p512', ...
    'texmos2.p512', 'texmos2.s512', 'texmos3.p512', 'texmos3b.p512', ...
    'texmos3.s512'};

sequences_names = { ...
    '6.1.01', '6.1.02', '6.1.03', '6.1.04', '6.1.05', '6.1.06', ...
    '6.1.07', '6.1.08', '6.1.09', '6.1.10', '6.1.11', '6.1.12', ...
    '6.1.13', '6.1.14', '6.1.15', '6.1.16', '6.2.01', '6.2.02', ...
    '6.2.03', '6.2.04', '6.2.05', '6.2.06', '6.2.07', '6.2.08', ...
    '6.2.09', '6.2.10', '6.2.11', '6.2.12', '6.2.13', '6.2.14', ...
    '6.2.15', '6.2.16', '6.2.17', '6.2.18', '6.2.19', '6.2.20', ...
    '6.2.21', '6.2.22', '6.2.23', '6.2.24', '6.2.25', '6.2.26', ...
    '6.2.27', '6.2.28', '6.2.29', '6.2.30', '6.2.31', '6.2.32', ...
    '6.3.01', '6.3.02', '6.3.03', '6.3.04', '6.3.05', '6.3.06', ...
    '6.3.07', '6.3.08', '6.3.09', '6.3.10', '6.3.11', 'motion01.512', ...
    'motion02.512', 'motion03.512', 'motion04.512', 'motion05.512', ...
    'motion06.512', 'motion07.512', 'motion08.512', 'motion09.512', ...
    'motion10.512'};

all_names = [misc_names, aerials_names, textures_names, sequences_names];
all_vols = [ ...
    repmat({'misc'},      1, numel(misc_names)), ...
    repmat({'aerials'},   1, numel(aerials_names)), ...
    repmat({'textures'},  1, numel(textures_names)), ...
    repmat({'sequences'}, 1, numel(sequences_names))];

fprintf('Downloading %d grayscale images (%d misc + %d aerials + %d textures + %d sequences)...\n', ...
    numel(all_names), numel(misc_names), numel(aerials_names), ...
    numel(textures_names), numel(sequences_names));

manifest_path = fullfile(img_dir, 'manifest_gray.csv');
mfid = fopen(manifest_path, 'w');
fprintf(mfid, 'filename,volume,name,height,width,channels\n');

skipped = {};
total_pixels = 0;

for i = 1:numel(all_names)
    name = all_names{i};
    vol = all_vols{i};
    dest = fullfile(img_dir, [name '.tiff']);

    if exist(dest, 'file')
        fprintf('[%3d/%3d] %-14s (%-9s) present\n', i, numel(all_names), name, vol);
    else
        url = sprintf('https://sipi.usc.edu/database/download.php?vol=%s&img=%s', vol, name);
        fprintf('[%3d/%3d] %-14s (%-9s) downloading...\n', i, numel(all_names), name, vol);
        try
            websave(dest, url);
        catch err
            fprintf('          FAILED: %s\n', err.message);
            skipped{end + 1} = sprintf('%s (download failed)', name); %#ok<SAGROW>
            continue;
        end
    end

    % Only native single-channel 8-bit images enter the manifest.
    info = imfinfo(dest);
    channels = info(1).SamplesPerPixel;
    if channels ~= 1 || info(1).BitDepth ~= 8
        fprintf('          EXCLUDED: %d channel(s), %d-bit -- not native 8-bit grayscale\n', ...
            channels, info(1).BitDepth);
        skipped{end + 1} = sprintf('%s (%d ch, %d-bit)', name, channels, info(1).BitDepth); %#ok<SAGROW>
        continue;
    end

    fprintf(mfid, '%s,%s,%s,%d,%d,%d\n', [name '.tiff'], vol, name, ...
        info(1).Height, info(1).Width, channels);
    total_pixels = total_pixels + double(info(1).Height) * double(info(1).Width);
end

fclose(mfid);

fprintf('\nDone. Wrote %s\n', manifest_path);
fprintf('%d of %d images are native 8-bit grayscale, %.2fM pixels total.\n', ...
    numel(all_names) - numel(skipped), numel(all_names), total_pixels / 1e6);
if ~isempty(skipped)
    fprintf('Excluded %d:\n', numel(skipped));
    fprintf('  %s\n', skipped{:});
end
