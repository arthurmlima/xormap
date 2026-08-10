# xormap RGB888 image cipher - native C++17 port

This folder is the self-contained C++ counterpart of the original RGB888
MATLAB implementation. It implements the full 24-bit packed RGB888 cipher,
translated correctness tests, the 51-image parallel sweep, and the combined
key-sensitivity/histogram/PSNR analysis.

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

The defaults read the included 51 RGB TIFFs and manifest from this project's
`images/` directory and write this folder's `results/` directory. No MATLAB
source or external data directory is required.

```sh
./build/xormap_rgb888 help
./build/xormap_rgb888 verify
./build/xormap_rgb888 run-tests --threads 8
```

`run-tests` runs the 51-image sweep and the key-sensitivity/histogram/PSNR
analysis in one execution and writes exactly one CSV and one PDF:
`sweep_k_rgb888.csv` has one row per (image, K) with every sweep and
analysis metric as columns, followed by a second table (after a blank line)
with the per-bit key-sensitivity study (one representative image, K x bit
position); `sweep_k_rgb888.pdf` combines every panel from both passes into
11 panels (no cross-format comparison output). Encryption/decryption timing
is not collected (it isn't a security metric and varies by machine load).
NPCR and UACI for a given perturbation share one panel/column pair instead
of two; columns and panels are labelled "plaintext bit flip" (sweep) versus
"key bit flip" (key sensitivity) — they measure different perturbations and
the numbers are not expected to match. Only the cipher's pixel histogram is
plotted (the plaintext's is a real photo and unsurprisingly non-uniform;
the "Plain vs cipher chi-square" panel already reports how non-uniform it
is quantitatively). It accepts `--images`, `--manifest`, `--results`,
`--threads`, `--k-first/--k-step/--k-last`, and `--samples` (used by the
sweep pass).

The standard grid is `K=24:24:384`: 51 images x 16 K values = 816 tasks.
Outputs reproduce the MATLAB CSV schemas and generate native Cairo vector
PDFs. Timing columns are naturally machine- and load-dependent.

See `docs/MATLAB_PORT_MAP.md` for the source-to-C++ mapping.
