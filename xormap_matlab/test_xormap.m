function test_xormap()
%TEST_XORMAP Cross-check xormap_transform against the actual generator.
%   Builds the same M matrix Genv_xormap.m builds (via demo_chunks.m and
%   extract_3section.m in ../xormapverilog), evaluates the pairwise-XOR
%   map straight from that matrix, and compares against xormap_transform
%   for every single-bit input and a batch of random inputs, for a range
%   of K. Mirrors the structure of xormap_cpp/test_xormap.cpp.

    this_dir = fileparts(mfilename('fullpath'));
    addpath(fullfile(this_dir, '..', 'xormapverilog'));

    rng(12345);

    for k = 5:40
        test_width(k);
    end

    fprintf('All XOR-map tests passed.\n');
end

function test_width(k)
    M = build_reference_matrix(k);

    for bit = 0:(k - 1)
        input = false(1, k);
        input(bit + 1) = true;
        assert(isequal(xormap_transform(input, k), reference_transform(M, k, input)), ...
            'mismatch at k=%d bit=%d', k, bit);
    end

    for sample = 1:20
        input = rand(1, k) > 0.5;
        assert(isequal(xormap_transform(input, k), reference_transform(M, k, input)), ...
            'mismatch at k=%d sample=%d', k, sample);
    end
end

function M = build_reference_matrix(k)
% Same steps as Genv_xormap.m up to (and including) the second fliplr,
% skipping the Verilog-writing part.
    a = demo_chunks(k);
    a(:, 1) = [];
    [M, ~, ~] = extract_3section(a, k, 0);
    M = fliplr(M);
end

function result = reference_transform(M, k, input)
% Same computation as the "next-state logic" loop in Genv_xormap.m.
    result = false(1, k);
    for col_indx = k:-1:1
        bit_idx = k - col_indx;
        n_not_empty = nnz(~cellfun(@isempty, M(:, col_indx)));

        value = false;
        for row_indx = 1:n_not_empty
            pair = M{row_indx, col_indx};
            value = xor(value, input(pair(1)));
            value = xor(value, input(pair(2)));
        end

        result(bit_idx + 1) = value;
    end
end
