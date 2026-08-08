# xormap RGB565 image cipher - native C++17 port

This folder ports `../xormap_image_rgb565_matlab` and removes its original
three-image limitation. It uses the same complete 51-image source manifest
as RGB888, converts every TIFF to packed RGB565, and can calculate metrics
for the entire converted corpus in parallel.

## Full RGB888-to-RGB565 corpus

Run:

```sh
make convert
```

This reads all 51 RGB888 TIFFs from
`../xormap_image_rgb888_matlab/images`, truncates channels exactly like
MATLAB (`R8>>3`, `G8>>2`, `B8>>3`), and writes 51 files under
`images_rgb565/` plus `images_rgb565/manifest.csv`.

Each `.rgb565` file is self-describing and deterministic:

```text
8 bytes  "RGB565BE"
4 bytes  width, unsigned big-endian
4 bytes  height, unsigned big-endian
2*N      packed pixels in row-major big-endian RGB565 order
```

The converter validates existing files against a fresh source conversion;
use `--overwrite` to rewrite them. The complete converted corpus includes
the 2250x2250 `wash-ir` image and occupies about 68 MiB.

## Build and test

```sh
make
make test
make verify
```

The tests verify all 51 converted files byte-for-word against their RGB888
sources and reproduce the published MATLAB K=512 Mandrill entropy,
NPCR/UACI and lossless round trip.

## Workflows

```sh
# Original MATLAB behavior: three K=512 reports, 9 PDFs and results.md
./build/xormap_rgb565 run-all

# Optionally generate those reports for every converted image
./build/xormap_rgb565 run-all --all

# Original three-image K=8:4:512 sweep
./build/xormap_rgb565 sweep

# New requested full-corpus parallel sweep: 51 x 16 = 816 tasks
./build/xormap_rgb565 sweep-all --threads 8
```

The full sweep defaults to `K=24:24:384` and reports normalized 5/6/5
channel entropy, mean absolute horizontal correlation, packed 16-bit
NPCR/UACI, iteration counts, pixels per iteration and two-encryption time.
It uses native C++ threads and MATLAB-compatible worker Threefry streams.

All path, K-grid, sample-count and worker settings are exposed in
`./build/xormap_rgb565 help`. `make full` performs conversion followed by
the full-corpus sweep.

See `docs/MATLAB_PORT_MAP.md` for the exact function mapping.
