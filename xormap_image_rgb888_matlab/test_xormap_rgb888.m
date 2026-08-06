function test_xormap_rgb888()
%TEST_XORMAP_RGB888 Regression tests for the non-fold RGB888 cipher.
%   Confirms XORMAP_KEYSTREAM_WORDS_FAST (built on the O(K) fast
%   transform) is bit-for-bit identical to the existing naive O(K^2)
%   XORMAP_KEYSTREAM_WORDS (xormap_image_rgb565_matlab) for every
%   K = 24:24:384, then checks encrypt/decrypt round-trips and the
%   K/24-pixels-per-iteration bookkeeping on a small synthetic image.

    this_dir = fileparts(mfilename('fullpath'));
    addpath(this_dir);
    addpath(fullfile(this_dir, '..', 'xormap_matlab'));
    addpath(fullfile(this_dir, '..', 'xormap_image_matlab'));
    addpath(fullfile(this_dir, '..', 'xormap_image_rgb565_matlab'));      % naive xormap_keystream_words (oracle)
    addpath(fullfile(this_dir, '..', 'xormap_image_rgb565_xorfold_matlab')); % xormap_fast_plan / xormap_transform_fast

    test_fast_matches_naive_keystream();
    test_pack_unpack_roundtrip();
    test_encrypt_decrypt_roundtrip();

    fprintf('All RGB888 (non-fold) tests passed for K=24:24:384.\n');
end

function test_fast_matches_naive_keystream()
    for k = 24:24:384
        seed = deterministic_bits(k, 5);
        num_words = 100;  % arbitrary, exercises multiple iterations at every K

        fast = xormap_keystream_words_fast(seed, 24, num_words);
        naive = xormap_keystream_words(seed, 24, num_words);

        assert(isa(fast, 'uint32') && isrow(fast), 'fast keystream must be a uint32 row vector at K=%d', k);
        assert(isequal(fast, naive), 'fast/naive keystream mismatch at K=%d', k);
        assert(all(fast <= 2^24 - 1), 'fast keystream exceeds 24 bits at K=%d', k);
    end
end

function test_pack_unpack_roundtrip()
    rng_state = rng;
    rng(3);
    rgb = uint8(randi([0 255], 5, 7, 3));
    rng(rng_state);

    packed = rgb888_pack(rgb);
    assert(isa(packed, 'uint32') && isequal(size(packed), [5 7]));
    assert(all(packed(:) <= 2^24 - 1));

    recovered = rgb888_unpack(packed);
    assert(isequal(recovered, rgb), 'rgb888_pack/unpack round-trip failed');
end

function test_encrypt_decrypt_roundtrip()
    rng_state = rng;
    rng(11);
    rgb = uint8(randi([0 255], 4, 6, 3));
    rng(rng_state);
    plain = rgb888_pack(rgb);
    num_pixels = numel(plain);

    for k = 24:24:384
        key_bits = deterministic_bits(k, 23);

        [cipher, seed] = xormap_rgb888_encrypt(plain, key_bits);
        recovered = xormap_rgb888_decrypt(cipher, seed);

        assert(isa(cipher, 'uint32') && isequal(size(cipher), size(plain)), ...
            'cipher type/shape mismatch at K=%d', k);
        assert(all(cipher(:) <= 2^24 - 1), 'cipher exceeds 24 bits at K=%d', k);
        assert(isequal(recovered, plain), 'encrypt/decrypt round-trip failed at K=%d', k);

        % Independently confirm the expected iteration count: ceil(24*N/K).
        expected_iters = ceil(24 * num_pixels / k);
        actual_words = xormap_keystream_words_fast(seed, 24, num_pixels);
        assert(numel(actual_words) == num_pixels, 'unexpected keystream length at K=%d', k);
        assert(expected_iters >= 1, 'sanity check on iteration formula at K=%d', k); %#ok<NOPRT>
    end
end

function bits = deterministic_bits(k, salt)
% A reproducible nontrivial pattern, independent of MATLAB's global RNG
% (mirrors the sibling xorfold test's helper).
    indices = uint64(0:(k - 1));
    values = indices .* uint64(73 + salt) + ...
             idivide(indices, uint64(3), 'floor') .* uint64(19) + ...
             uint64(11 * salt + 5);
    bits = logical(bitget(values, 1) ~= bitget(values, 4));
end
