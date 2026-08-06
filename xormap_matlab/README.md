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

Run the comparison test, which checks `xormap_transform` against the
actual matrix built by `demo_chunks.m` and `extract_3section.m` in
`../xormapverilog`, for K = 5..40:

```matlab
test_xormap
```
