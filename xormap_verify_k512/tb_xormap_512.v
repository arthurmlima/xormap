`timescale 1ns/1ps
`include "test_params.vh"

// Replays test_vectors.mem (produced by gen_test_vectors.m, independently
// cross-checked against xormap_cpp in cpp_check/) against the actual
// generated xormap_512 hardware. For each seed: reset, load the seed,
// then pulse `en` `TB_NUM_STEPS` times, checking that the registered
// output `s` matches the expected iteration after every pulse.

module tb_xormap_512;

    localparam K             = `TB_K;
    localparam NUM_SEEDS     = `TB_NUM_SEEDS;
    localparam NUM_STEPS     = `TB_NUM_STEPS;
    localparam LINES_PER_SEED = NUM_STEPS + 1;
    localparam TOTAL_LINES    = NUM_SEEDS * LINES_PER_SEED;

    reg              clk;
    reg              rst;
    reg              load;
    reg              en;
    reg  [K-1:0]     a;
    wire [K-1:0]     s;

    reg  [K-1:0]     vectors [0:TOTAL_LINES-1];

    integer errors;
    integer checked;
    integer seed_idx;
    integer step_idx;
    integer base;

    xormap_512 dut (
        .clk  (clk),
        .rst  (rst),
        .load (load),
        .en   (en),
        .a    (a),
        .s    (s)
    );

    always #5 clk = ~clk;

    initial begin
        clk     = 0;
        rst     = 1;
        load    = 0;
        en      = 0;
        a       = {K{1'b0}};
        errors  = 0;
        checked = 0;

        $readmemh("test_vectors.mem", vectors);

        @(posedge clk); #1;
        @(posedge clk); #1;
        rst = 0;

        for (seed_idx = 0; seed_idx < NUM_SEEDS; seed_idx = seed_idx + 1) begin
            base = seed_idx * LINES_PER_SEED;

            rst = 1;
            @(posedge clk); #1;
            rst = 0;

            a = vectors[base];
            load = 1;
            @(posedge clk); #1;
            load = 0;

            if (s !== vectors[base]) begin
                errors = errors + 1;
                $display("MISMATCH seed=%0d after load: expected %h got %h",
                          seed_idx, vectors[base], s);
            end

            for (step_idx = 1; step_idx <= NUM_STEPS; step_idx = step_idx + 1) begin
                en = 1;
                @(posedge clk); #1;
                en = 0;
                checked = checked + 1;

                if (s !== vectors[base + step_idx]) begin
                    errors = errors + 1;
                    $display("MISMATCH seed=%0d step=%0d: expected %h got %h",
                              seed_idx, step_idx, vectors[base + step_idx], s);
                end else begin
                    $display("OK seed=%0d step=%0d s=%h", seed_idx, step_idx, s);
                end
            end
        end

        $display("Checked %0d iterations across %0d seeds at K=%0d.",
                  checked, NUM_SEEDS, K);
        if (errors == 0) begin
            $display("PASS: xormap_512 hardware matches MATLAB/C++ for every iteration.");
        end else begin
            $display("FAIL: %0d mismatch(es).", errors);
        end

        $finish;
    end

endmodule
