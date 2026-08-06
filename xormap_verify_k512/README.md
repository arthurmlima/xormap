# K=512 three-way validation

Generates the `xormap_512` Verilog module and checks it against both other
implementations at K=512: [xormap_cpp](../xormap_cpp) and
[xormap_matlab](../xormap_matlab). None of the generated artifacts are
checked into git (see `.gitignore`) — run the steps below to reproduce them.

## Pipeline

1. **`generate_v512.m`** — same generator as
   [`../xormapverilog/Genv_xormap.m`](../xormapverilog/Genv_xormap.m),
   parameterized to `K=512`. Writes `xormap_512.v` (~7.3 MB, ~197k lines —
   `O(K^2)` XOR-pair wires/terms at this width).

2. **`gen_test_vectors.m`** — ground truth. Picks 6 seeds (all-zero,
   all-one, single bit set at each end, two pseudo-random 512-bit values),
   and for each one iterates `xormap_matlab`'s `xormap_transform` 6 times.
   Writes:
   - `test_vectors.mem` — one 128-hex-digit value per line, `$readmemh`-ready:
     seed, then 6 successive iterations, per seed block (42 lines total).
   - `test_params.vh` / `test_params.hpp` — `K`/seed-count/step-count
     constants shared by the Verilog testbench and the C++ checker, so all
     three consumers can't drift out of sync with each other.

3. **`cpp_check/check_k512.cpp`** — independently recomputes every
   iteration from each seed using `xormap::transform` (`xormap_cpp`,
   arbitrary-width `boost::multiprecision::cpp_int`) and diffs against the
   corresponding line already written by MATLAB. It never uses the
   "expected" lines as input, only as the comparison target.

4. **`tb_xormap_512.v`** — replays the same `test_vectors.mem` against the
   actual generated `xormap_512` hardware: reset, load the seed, then pulse
   `en` once per iteration, checking the registered output `s` against the
   expected line after each pulse.

## Results (last run)

```
C++ (xormap_cpp) matches MATLAB (xormap_matlab) for every iteration.
Checked 36 iterations across 6 seeds at K=512.
```

```
Checked 36 iterations across 6 seeds at K=512.
PASS: xormap_512 hardware matches MATLAB/C++ for every iteration.
```

All three implementations agree on all 36 iterations (6 seeds x 6 steps).

## Reproducing

```sh
# 1. Generate the Verilog module and the test vectors (MATLAB)
matlab -batch "generate_v512"
matlab -batch "gen_test_vectors"

# 2. Cross-check C++ against the MATLAB-generated vectors
cd cpp_check && make check && cd ..

# 3. Simulate the generated hardware against the same vectors
IVERILOG=/opt/oss-cad-suite-linux-x64-20260728/oss-cad-suite/bin/iverilog
VVP=/opt/oss-cad-suite-linux-x64-20260728/oss-cad-suite/bin/vvp
$IVERILOG -g2012 -o xormap_512_tb.vvp xormap_512.v tb_xormap_512.v
$VVP xormap_512_tb.vvp
```

Step 3 is the slow part: `iverilog` takes a few minutes to elaborate the
~197k-line generated module at this width (compiling `xormap_512.v` alone
took ~5 min on this machine; simulation itself runs in ~20s). This is a
one-off verification module, not something meant to be regenerated on
every change — the C++ and MATLAB implementations are the fast paths for
actually computing the transform.
