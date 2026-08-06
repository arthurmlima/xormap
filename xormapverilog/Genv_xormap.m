clear all;
clc
K=32;
l=0;





[a]=demo_chunks(K);
%M=merge_cells_append_multi(a,b,c);
a(:,1)=[];
[M m1 m2]=extract_3section(a,K,l);

a = fliplr(a);
M = fliplr(M);

pretty_func(M)

%m1 = fliplr(m1);
%m2 = fliplr(m2);
%M = fill_blanks(M, m1, m2);
%pretty_func(M)

% for even k even.
%M = fill_missing_pair(M, K);

%pretty_func(M);





filename=sprintf('xormap_%d.v',K);

% Open the file for writing
fileID = fopen(filename, 'w');

% Check if the file opened successfully
if fileID == -1
    error('Failed to open the file for writing.');
end

% Write Verilog code to the file
fprintf(fileID, '// Auto-generated XOR-map module.\n');
fprintf(fileID, '// K = %d\n\n', K);

fprintf(fileID, 'module xormap_%d (\n', K);
fprintf(fileID, '    input  wire         clk,\n');
fprintf(fileID, '    input  wire         rst,\n');
fprintf(fileID, '    input  wire         load,\n');
fprintf(fileID, '    input  wire         en,\n');
fprintf(fileID, '    input  wire [%d:0]  a,\n', K-1);
fprintf(fileID, '    output wire [%d:0]  s\n', K-1);
fprintf(fileID, ');\n\n');

fprintf(fileID, '  reg  [%d:0] x_reg = {%d{1''b0}};\n', K-1, K);
fprintf(fileID, '  wire [%d:0] x_next;\n\n', K-1);

[nRows, nCols] = size(M);

% Declare one Verilog wire for each XOR pair used by the map.
declared_pairs = {};
for j = 1:nRows
    for i = 1:nCols
        v = M{j,i};

        % Only accept a numeric 1x2 or 2x1 vector like [7 8].
        if isnumeric(v) && isvector(v) && numel(v) == 2
            ou = v(:).';  % force row [x y]
            key = sprintf('%d_%d', ou(1), ou(2));

            if ~ismember(key, declared_pairs)
                declared_pairs{end+1} = key;
                fprintf(fileID, '  wire s_%d_%d;\n', ou(1), ou(2));
            end
        end
    end
end

fprintf(fileID, '\n');

[R, C] = size(M);

% Generate the pairwise XOR assignments.
assigned_pairs = {};
for i = C-K+1:C                  % loop columns
    for j = 1:R                  % loop rows within column, top to bottom

        v = M{j,i};

        % Break column if null/empty.
        if isempty(v)
            break;
        end

        % Safety check.
        if numel(v) ~= 2
            error('M{%d,%d} must be a 1x2 pair.', j, i);
        end

        x = v(1);
        y = v(2);
        key = sprintf('%d_%d', x, y);

        if ~ismember(key, assigned_pairs)
            assigned_pairs{end+1} = key;
            fprintf(fileID, '  assign s_%d_%d = x_reg[%d] ^ x_reg[%d];\n', ...
                x, y, x-1, y-1);
        end
    end
end

fprintf(fileID, '\n');

% Generate the next-state logic.
for Colum_indx = K:-1:1
    bit_idx = K - Colum_indx;
    n_not_empty = nnz(~cellfun(@isempty, M(:, Colum_indx)));

    terms = {};
    for Row_indx = 1:n_not_empty
        o = M{Row_indx, Colum_indx};
        terms{end+1} = sprintf('s_%d_%d', o(1), o(2));
    end

    if isempty(terms)
        expr = '1''b0';
    else
        expr = strjoin(terms, ' ^ ');
    end

    fprintf(fileID, '  assign x_next[%d] = %s;\n', bit_idx, expr);
end

fprintf(fileID, '\n');

% Sequential state register.
fprintf(fileID, '  always @(posedge clk) begin\n');
fprintf(fileID, '    if (rst) begin\n');
fprintf(fileID, '      x_reg <= {%d{1''b0}};\n', K);
fprintf(fileID, '    end else if (load) begin\n');
fprintf(fileID, '      x_reg <= a;\n');
fprintf(fileID, '    end else if (en) begin\n');
fprintf(fileID, '      x_reg <= x_next;\n');
fprintf(fileID, '    end\n');
fprintf(fileID, '  end\n\n');

fprintf(fileID, '  assign s = x_reg;\n\n');
fprintf(fileID, 'endmodule\n');

fclose(fileID);
