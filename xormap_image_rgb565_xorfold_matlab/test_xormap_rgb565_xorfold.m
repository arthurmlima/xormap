function test_xormap_rgb565_xorfold()
%TEST_XORMAP_RGB565_XORFOLD Regression tests for one-folded-state-per-pixel.
%   Uses a small synthetic RGB565 image and checks every K = 32:32:512.
%   The expected keystream is generated independently with the canonical
%   xormap_transform and a local fold implementation.

    this_dir = fileparts(mfilename('fullpath'));
    addpath(this_dir);
    addpath(fullfile(this_dir, '..', 'xormap_matlab'));
    addpath(fullfile(this_dir, '..', 'xormap_image_matlab'));
    addpath(fullfile(this_dir, '..', 'xormap_image_rgb565_matlab'));

    test_known_fold();

    plain = uint16([ ...
        0,     1,     31,  2016; ...
        63488, 65535, 4660, 22136; ...
        43690, 21845, 3855, 61680]);
    num_pixels = numel(plain);

    for k = 32:32:512
        plan = xormap_fast_plan(k);
        test_fast_against_canonical(k, plan);

        generator_seed = deterministic_bits(k, 17);
        [actual_words, actual_final_state, generator_iterations] = ...
            xormap_keystream_xorfold16(generator_seed, num_pixels);
        [expected_words, expected_final_state] = ...
            canonical_keystream(generator_seed, num_pixels);

        assert(isa(actual_words, 'uint16') && isrow(actual_words), ...
            'keystream must be a uint16 row vector at K=%d', k);
        assert(isequal(actual_words, expected_words), ...
            'canonical keystream mismatch at K=%d', k);
        assert(isequal(logical(actual_final_state(:).'), expected_final_state), ...
            'final-state mismatch at K=%d', k);
        assert(generator_iterations == num_pixels, ...
            'generator used %d iterations for %d pixels at K=%d', ...
            generator_iterations, num_pixels, k);

        key_bits = deterministic_bits(k, 29);
        [cipher, seed, encrypt_iterations] = ...
            xormap_rgb565_xorfold_encrypt(plain, key_bits);
        [recovered, decrypt_iterations] = ...
            xormap_rgb565_xorfold_decrypt(cipher, seed);

        assert(isa(cipher, 'uint16') && isequal(size(cipher), size(plain)), ...
            'cipher type or shape mismatch at K=%d', k);
        assert(isequal(recovered, plain), ...
            'encrypt/decrypt round trip failed at K=%d', k);
        assert(encrypt_iterations == num_pixels, ...
            'encryption used %d iterations for %d pixels at K=%d', ...
            encrypt_iterations, num_pixels, k);
        assert(decrypt_iterations == num_pixels, ...
            'decryption used %d iterations for %d pixels at K=%d', ...
            decrypt_iterations, num_pixels, k);

        % Confirm independently that encryption used one canonical folded
        % state for each row-major RGB565 pixel.
        [expected_encrypt_words, ~] = canonical_keystream(seed, num_pixels);
        plain_words = reshape(plain.', 1, []);
        cipher_words = reshape(cipher.', 1, []);
        assert(isequal(cipher_words, bitxor(plain_words, expected_encrypt_words)), ...
            'cipher does not use the canonical per-pixel words at K=%d', k);
    end

    fprintf(['All RGB565 XOR-fold tests passed for K=32:32:512 ', ...
             '(%d iterations per transform/encrypt/decrypt check).\n'], ...
            num_pixels);
end

function test_known_fold()
    state = [word_to_bits(uint16(hex2dec('1234'))), ...
             word_to_bits(uint16(hex2dec('5678')))];
    expected = uint16(hex2dec('444C'));
    actual = xor_fold16(state);

    assert(isa(actual, 'uint16') && isequal(actual, expected), ...
        'known fold failed: 0x1234 XOR 0x5678 must equal 0x444C');
end

function test_fast_against_canonical(k, plan)
    % Both transforms are linear over GF(2).  Equality for every basis
    % vector therefore proves equality for every possible K-bit state.
    for input_bit = 1:k
        input = false(1, k);
        input(input_bit) = true;
        expected = xormap_transform(input, k);
        actual = xormap_transform_fast(input, plan);
        assert(isequal(logical(actual(:).'), expected), ...
            'fast/canonical basis mismatch at K=%d input bit=%d', k, input_bit - 1);
    end

    for sample = 1:3
        input = deterministic_bits(k, sample);
        expected = xormap_transform(input, k);
        actual = xormap_transform_fast(input, plan);
        assert(isequal(logical(actual(:).'), expected), ...
            'fast/canonical transform mismatch at K=%d sample=%d', k, sample);
    end
end

function bits = deterministic_bits(k, salt)
% A reproducible nontrivial pattern, independent of MATLAB's global RNG.
    indices = uint64(0:(k - 1));
    values = indices .* uint64(73 + salt) + ...
             idivide(indices, uint64(3), 'floor') .* uint64(19) + ...
             uint64(11 * salt + 5);
    bits = logical(bitget(values, 1) ~= bitget(values, 4));
end

function [words, state] = canonical_keystream(seed_bits, num_words)
% Slow reference: canonical state transform plus an independent XOR fold.
    k = numel(seed_bits);
    state = logical(seed_bits(:).');
    words = zeros(1, num_words, 'uint16');

    for idx = 1:num_words
        state = xormap_transform(state, k);
        words(idx) = canonical_fold16(state);
    end
end

function word = canonical_fold16(state_bits)
% Intentionally does not call xor_fold16: this is the test oracle.
    word = uint16(0);
    for output_bit = 0:15
        parity = false;
        for source = (output_bit + 1):16:numel(state_bits)
            parity = xor(parity, logical(state_bits(source)));
        end
        if parity
            word = bitset(word, output_bit + 1, 1);
        end
    end
end

function bits = word_to_bits(word)
    bits = false(1, 16);
    for bit = 0:15
        bits(bit + 1) = logical(bitget(word, bit + 1));
    end
end
