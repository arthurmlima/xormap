function test_xormap_gray_fast()
%TEST_XORMAP_GRAY_FAST Prove the fast grayscale path == the canonical one.
%   sweep_k_gray_all.m runs on XORMAP_KEYSTREAM_FAST /
%   XORMAP_IMAGE_ENCRYPT_FAST for tractability. Those are only legitimate
%   if they reproduce XORMAP_KEYSTREAM / XORMAP_IMAGE_ENCRYPT exactly, so
%   this asserts equality directly rather than trusting the reformulation:
%
%     1. the fast transform matches the canonical one on every single-bit
%        basis state (both are linear over GF(2), so agreeing on a basis
%        proves agreement on all 2^K states);
%     2. the fast keystream is byte-for-byte the canonical keystream;
%     3. the fast encryption is pixel-for-pixel the canonical encryption;
%     4. the canonical decrypt still inverts the fast encrypt;
%     5. the reported iteration count is ceil(N*8/K).

    this_dir = fileparts(mfilename('fullpath'));
    addpath(this_dir);
    addpath(fullfile(this_dir, '..', 'xormap_matlab'));

    K_VALUES = 24:24:384;

    plain = uint8([ ...
          0,   1, 255, 127,  64; ...
        200,  17,  42,  99, 128; ...
         13, 240,   7, 181,  55; ...
        170,  85, 100,   3, 222]);
    num_pixels = numel(plain);

    for k = K_VALUES
        plan = xormap_fast_plan(k);

        % 1. basis-state equivalence of the two transforms
        for input_bit = 1:k
            input = false(1, k);
            input(input_bit) = true;
            actual = xormap_transform_fast(input, plan);
            assert(isequal(logical(actual(:).'), xormap_transform(input, k)), ...
                'fast/canonical basis mismatch at K=%d bit=%d', k, input_bit - 1);
        end

        % 2. keystream equality (ask for a length that is not a multiple of
        %    K/8, so the final partial iteration is exercised too)
        seed = deterministic_bits(k, 11);
        for num_bytes = [1, 7, num_pixels, 3 * k + 5]
            assert(isequal(xormap_keystream_fast(seed, num_bytes), ...
                           xormap_keystream(seed, num_bytes)), ...
                'keystream mismatch at K=%d for %d bytes', k, num_bytes);
        end

        % 3./4./5. whole-image equality, round trip and iteration count
        key_bits = secret_key(k);
        [cipher_fast, seed_fast, iterations] = xormap_image_encrypt_fast(plain, key_bits);
        [cipher_ref,  seed_ref]              = xormap_image_encrypt(plain, key_bits);

        assert(isequal(logical(seed_fast(:).'), logical(seed_ref(:).')), ...
            'seed mismatch at K=%d', k);
        assert(isa(cipher_fast, 'uint8') && isequal(size(cipher_fast), size(plain)), ...
            'cipher type or shape mismatch at K=%d', k);
        assert(isequal(cipher_fast, cipher_ref), ...
            'fast/canonical ciphertext mismatch at K=%d', k);
        assert(isequal(xormap_image_decrypt(cipher_fast, seed_fast), plain), ...
            'round trip failed at K=%d', k);
        assert(iterations == ceil(num_pixels * 8 / k), ...
            'iteration count %d != ceil(%d*8/%d) at K=%d', ...
            iterations, num_pixels, k, k);
    end

    fprintf(['All grayscale fast-path tests passed for K=24:24:384 ', ...
             '(fast == canonical transform, keystream, ciphertext; round trip OK).\n']);
end

function bits = deterministic_bits(k, salt)
% Reproducible nontrivial pattern, independent of MATLAB's global RNG.
    indices = uint64(0:(k - 1));
    values = indices .* uint64(73 + salt) + ...
             idivide(indices, uint64(3), 'floor') .* uint64(19) + ...
             uint64(11 * salt + 5);
    bits = logical(bitget(values, 1) ~= bitget(values, 4));
end
