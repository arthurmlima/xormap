# MATLAB XOR-map model

The MATLAB counterpart to [xormap_cpp](../xormap_cpp): one function that
accepts a `K`-bit input and returns the `K`-bit result produced by
`Genv_xormap.m`, without generating any Verilog.

```matlab
K = 32;
input = xormap_uint2bits(hex2dec('12345678'), K);
result_bits = xormap_transform(input, K);
result = xormap_bits2uint(result_bits);
```

`xormap_transform` works directly on bit vectors (`1xK` logical arrays,
bit 1 = LSB), so it isn't limited to widths that fit in a native integer
type — the same role `boost::multiprecision::cpp_int` plays in the C++
version. `xormap_uint2bits`/`xormap_bits2uint` are convenience wrappers
for `K <= 53` (double precision integer range); for wider `K`, build or
consume the bit vector directly.

## The fast transform

`xormap_transform` visits every symmetric pair in its window, which is
O(K²) per state update. `xormap_transform_fast` is a prefix-XOR
reformulation of exactly the same map that costs O(K), driven by an index
plan precomputed once per K:

```matlab
plan = xormap_fast_plan(K);
result_bits = xormap_transform_fast(input, plan);   % == xormap_transform(input, K)
```

The two are sequence-equivalent by construction. Both maps are linear over
GF(2), so agreeing on every single-bit basis state proves they agree on all
2^K states — which is what the image projects' tests check per K
(`../xormap_image_matlab/test_xormap_gray_fast.m`,
`../xormap_image_rgb888_matlab/test_xormap_rgb888.m`). The database-wide
sweeps would be impractical at O(K²), so they all run on the fast form.

Keep both here: the canonical version is the specification, the fast one
is what the sweeps execute, and the tests exist to hold them together.

Run the comparison test, which checks `xormap_transform` against the
actual matrix built by `demo_chunks.m` and `extract_3section.m` in
`../xormapverilog`, for K = 5..40:

```matlab
test_xormap
```
