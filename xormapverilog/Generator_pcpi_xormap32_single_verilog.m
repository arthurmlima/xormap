clear all;
clc;

K = 32;
l = 0;

if K ~= 32
    error('This PCPI generator is written for K = 32 because PicoRV32 PCPI uses 32-bit operands.');
end

[a] = demo_chunks(K);
% M = merge_cells_append_multi(a,b,c);
a(:,1) = [];
[M, m1, m2] = extract_3section(a, K, l);

a = fliplr(a);
M = fliplr(M);

pretty_func(M)

% m1 = fliplr(m1);
% m2 = fliplr(m2);
% M = fill_blanks(M, m1, m2);
% pretty_func(M)

% for even k even.
% M = fill_missing_pair(M, K);
% pretty_func(M);

pcpi_module = sprintf('picorv32_pcpi_xormap%d', K);
core_module = sprintf('xormap_%d', K);
filename    = sprintf('pcpi_xormap%d.v', K);

fileID = fopen(filename, 'w');

if fileID == -1
    error('Failed to open the file for writing.');
end

fprintf(fileID, '// Auto-generated PCPI XOR-map instruction.\n');
fprintf(fileID, '// K = %d\n', K);
fprintf(fileID, '// This file contains both the PCPI wrapper and the XOR-map core.\n\n');

% -------------------------------------------------------------------------
% PCPI wrapper
% -------------------------------------------------------------------------

fprintf(fileID, 'module %s (\n', pcpi_module);
fprintf(fileID, '    input  wire        clk,\n');
fprintf(fileID, '    input  wire        resetn,\n\n');
fprintf(fileID, '    input  wire        pcpi_valid,\n');
fprintf(fileID, '    input  wire [31:0] pcpi_insn,\n');
fprintf(fileID, '    input  wire [31:0] pcpi_rs1,\n');
fprintf(fileID, '    input  wire [31:0] pcpi_rs2,\n');
fprintf(fileID, '    output reg         pcpi_wr,\n');
fprintf(fileID, '    output reg  [31:0] pcpi_rd,\n');
fprintf(fileID, '    output reg         pcpi_wait,\n');
fprintf(fileID, '    output reg         pcpi_ready\n');
fprintf(fileID, ');\n\n');

fprintf(fileID, '  localparam [6:0] OPCODE_CUSTOM1  = 7''b0101011;\n');
fprintf(fileID, '  localparam [2:0] FUNCT3_XORMAP32 = 3''b000;\n');
fprintf(fileID, '  localparam [6:0] FUNCT7_XORMAP32 = 7''b0000000;\n\n');

fprintf(fileID, '  wire instr_xormap32 =\n');
fprintf(fileID, '      pcpi_valid &&\n');
fprintf(fileID, '      pcpi_insn[6:0]   == OPCODE_CUSTOM1 &&\n');
fprintf(fileID, '      pcpi_insn[14:12] == FUNCT3_XORMAP32 &&\n');
fprintf(fileID, '      pcpi_insn[31:25] == FUNCT7_XORMAP32;\n\n');

fprintf(fileID, '  localparam [1:0] STATE_IDLE = 2''d0;\n');
fprintf(fileID, '  localparam [1:0] STATE_LOAD = 2''d1;\n');
fprintf(fileID, '  localparam [1:0] STATE_STEP = 2''d2;\n');
fprintf(fileID, '  localparam [1:0] STATE_DONE = 2''d3;\n\n');

fprintf(fileID, '  reg [1:0]  state = STATE_IDLE;\n');
fprintf(fileID, '  reg [31:0] input_reg = 32''b0;\n\n');

fprintf(fileID, '  wire [31:0] map_s;\n');
fprintf(fileID, '  wire        map_load = state == STATE_LOAD;\n');
fprintf(fileID, '  wire        map_en   = state == STATE_STEP;\n\n');

fprintf(fileID, '  // rs2 is intentionally unused by this one-input custom instruction.\n');
fprintf(fileID, '  // verilator lint_off UNUSED\n');
fprintf(fileID, '  wire [31:0] unused_pcpi_rs2 = pcpi_rs2;\n');
fprintf(fileID, '  // verilator lint_on UNUSED\n\n');

fprintf(fileID, '  %s xormap32_core (\n', core_module);
fprintf(fileID, '      .clk  (clk),\n');
fprintf(fileID, '      .rst  (!resetn),\n');
fprintf(fileID, '      .load (map_load),\n');
fprintf(fileID, '      .en   (map_en),\n');
fprintf(fileID, '      .a    (input_reg),\n');
fprintf(fileID, '      .s    (map_s)\n');
fprintf(fileID, '  );\n\n');

fprintf(fileID, '  always @(posedge clk) begin\n');
fprintf(fileID, '    if (!resetn) begin\n');
fprintf(fileID, '      state      <= STATE_IDLE;\n');
fprintf(fileID, '      input_reg  <= 32''b0;\n');
fprintf(fileID, '      pcpi_wr    <= 1''b0;\n');
fprintf(fileID, '      pcpi_rd    <= 32''b0;\n');
fprintf(fileID, '      pcpi_wait  <= 1''b0;\n');
fprintf(fileID, '      pcpi_ready <= 1''b0;\n');
fprintf(fileID, '    end else begin\n');
fprintf(fileID, '      pcpi_wr    <= 1''b0;\n');
fprintf(fileID, '      pcpi_rd    <= 32''bx;\n');
fprintf(fileID, '      pcpi_wait  <= 1''b0;\n');
fprintf(fileID, '      pcpi_ready <= 1''b0;\n\n');

fprintf(fileID, '      case (state)\n');
fprintf(fileID, '        STATE_IDLE: begin\n');
fprintf(fileID, '          if (instr_xormap32) begin\n');
fprintf(fileID, '            input_reg <= pcpi_rs1;\n');
fprintf(fileID, '            pcpi_wait <= 1''b1;\n');
fprintf(fileID, '            state     <= STATE_LOAD;\n');
fprintf(fileID, '          end\n');
fprintf(fileID, '        end\n\n');

fprintf(fileID, '        STATE_LOAD: begin\n');
fprintf(fileID, '          pcpi_wait <= 1''b1;\n');
fprintf(fileID, '          state     <= STATE_STEP;\n');
fprintf(fileID, '        end\n\n');

fprintf(fileID, '        STATE_STEP: begin\n');
fprintf(fileID, '          pcpi_wait <= 1''b1;\n');
fprintf(fileID, '          state     <= STATE_DONE;\n');
fprintf(fileID, '        end\n\n');

fprintf(fileID, '        STATE_DONE: begin\n');
fprintf(fileID, '          pcpi_wr    <= 1''b1;\n');
fprintf(fileID, '          pcpi_ready <= 1''b1;\n');
fprintf(fileID, '          pcpi_rd    <= map_s;\n');
fprintf(fileID, '          state      <= STATE_IDLE;\n');
fprintf(fileID, '        end\n\n');

fprintf(fileID, '        default: begin\n');
fprintf(fileID, '          state <= STATE_IDLE;\n');
fprintf(fileID, '        end\n');
fprintf(fileID, '      endcase\n');
fprintf(fileID, '    end\n');
fprintf(fileID, '  end\n\n');

fprintf(fileID, 'endmodule\n\n');

% -------------------------------------------------------------------------
% XOR-map core
% -------------------------------------------------------------------------

fprintf(fileID, 'module %s (\n', core_module);
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

declared_pairs = {};
for j = 1:nRows
    for i = 1:nCols
        v = M{j,i};

        if isnumeric(v) && isvector(v) && numel(v) == 2
            ou = v(:).';
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

assigned_pairs = {};
for i = C-K+1:C
    for j = 1:R
        v = M{j,i};

        if isempty(v)
            break;
        end

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

fprintf('Generated %s containing modules %s and %s.\n', filename, pcpi_module, core_module);
