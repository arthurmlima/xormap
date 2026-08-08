# xormap RGB888 image cipher - native C++17 port

This folder is the C++ counterpart of
`../xormap_image_rgb888_matlab`. It implements the full 24-bit packed
RGB888 cipher, translated correctness tests, the 51-image parallel sweep,
and the combined key-sensitivity/histogram/PSNR analysis.

Pixels are packed exactly as MATLAB does:

```text
word[23:16] = R, word[15:8] = G, word[7:0] = B
```

The hash input serializes each packed word as big-endian `R,G,B` bytes.
Keystream bits remain LSB-first, the xormap state advances before each
emitted block, and arbitrary K boundaries are preserved. The fast O(K)
transform is used for production; the canonical O(K^2) implementation is
retained as a test oracle.

## Parallel and MATLAB random-stream parity

Every `(image,K)` pair is a native C++ task scheduled by `std::thread`.
MATLAB process workers retain the Threefry generator when the scripts call
bare `rng(seed)`, so the C++ sweep and analysis use the matching
Threefry4x64-20 key and correlation streams. Client-side work uses the
matching MATLAB Twister stream. This distinction is covered by real-image
golden tests.

## Build and test

Homebrew dependencies are the same as the grayscale port: CMake, OpenSSL 3,
libtiff, Catch2, Cairo and Poppler.

```sh
make
make test
make verify
```

The project reuses the already-tested transform, metrics, RNG, parallel and
vector-plot modules from `../xormap_image_matlab_cpp`; its packed-colour
engine lives in `include/xormap_color/common.hpp` and `src/common.cpp`.

## Commands

The defaults read the existing 51 RGB TIFFs and manifest from
`../xormap_image_rgb888_matlab/images` and write this folder's `results/`.

```sh
./build/xormap_rgb888 help
./build/xormap_rgb888 verify
./build/xormap_rgb888 sweep --threads 8
./build/xormap_rgb888 analysis --threads 8
```

Both large commands accept `--images`, `--manifest`, `--results`,
`--threads`, and `--k-first/--k-step/--k-last`. `sweep` also accepts
`--samples`.

The standard grid is `K=24:24:384`: 51 images x 16 K values = 816 tasks.
Outputs reproduce the MATLAB CSV schemas and generate native Cairo vector
PDFs. Timing columns are naturally machine- and load-dependent.

See `docs/MATLAB_PORT_MAP.md` for the source-to-C++ mapping.
