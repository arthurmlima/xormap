# MATLAB-to-C++ port map

Every `.m` source in `xormap_image_matlab` has a native C++ counterpart. The
table below maps the original entry point to its implementation and, where
applicable, its `xormap_gray` command. Public library declarations live under
`include/xormap_image/`; definitions live under `src/`.

| MATLAB source | Native C++ counterpart | CLI/workflow |
|---|---|---|
| `adjacent_correlation.m` | `metrics.hpp` / `metrics.cpp`: `adjacent_correlation`, including optional sampled pairs | Used by `run-all`, `sweep`, and `sweep-all` |
| `analysis_gray.m` | `evaluation.hpp` / `evaluation.cpp`: `analyze_all_images`; `metrics.cpp` for checksum, chi-square, and PSNR; `parallel.hpp` for task and bit-position parallelism | `analysis` |
| `bits_to_bytes.m` | `core.hpp` / `core.cpp`: `bits_to_bytes` | Library primitive and parity tests |
| `bytes_to_bits.m` | `core.hpp` / `core.cpp`: `bytes_to_bits` | Library primitive and parity tests |
| `chi_square_uniformity.m` | `metrics.hpp` / `metrics.cpp`: `chi_square_uniformity`, including the same Wilson-Hilferty/Acklam critical-value approximation | `analysis` |
| `compare_rgb888_vs_gray.m` | `evaluation.hpp` / `evaluation.cpp`: `read_sweep_csv` and `compare_sweeps`; `plot.cpp` for the three comparison reports | `compare` |
| `download_images.m` | `download.hpp` / `download.cpp`: three-image specification and `download_sipi_image`; orchestration in `evaluation.cpp` | `download` |
| `download_images_all_gray.m` | `download.hpp` / `download.cpp`: 159-image specification; TIFF validation and manifest writing in `evaluation.cpp` | `download --all` |
| `hash_expand_bits.m` | `core.hpp` / `core.cpp`: `hash_expand_bits` | Library primitive used by encryption |
| `npcr_uaci.m` | `metrics.hpp` / `metrics.cpp`: `npcr_uaci` | Used by all evaluation workflows |
| `npcr_uaci_ideal.m` | `metrics.hpp` / `metrics.cpp`: `npcr_uaci_ideal` | Evaluation and normalized sweep metadata |
| `psnr_db.m` | `metrics.hpp` / `metrics.cpp`: `psnr_db` | `analysis` |
| `read_sweep.m` | `evaluation.hpp` / `evaluation.cpp`: `read_sweep_csv`, normalized row/metadata types, and `print_sweep_summary` | `read-sweep gray`, `read-sweep rgb888`, or `read-sweep CSV` |
| `run_all.m` | `evaluation.hpp` / `evaluation.cpp`: `run_all`; TIFF artifacts through `image.cpp`; PDF panels through `plot.cpp` | `run-all` |
| `secret_key.m` | `core.hpp` / `core.cpp`: client-side Twister key; `threefry.hpp` / `threefry.cpp` and `evaluation.cpp`: Threefry worker key for the original `parfor` call sites | Library primitive used by encryption |
| `sha256_bits.m` | `core.hpp` / `core.cpp`: OpenSSL EVP `sha256_bytes`, followed by the same LSB-first bit expansion | Library primitive used by `hash_expand_bits` |
| `shannon_entropy.m` | `metrics.hpp` / `metrics.cpp`: `shannon_entropy` | Used by `run-all`, `sweep`, and `sweep-all` |
| `sweep_k.m` | `evaluation.hpp` / `evaluation.cpp`: `sweep_three_images` | `sweep` |
| `sweep_k_gray_all.m` | `evaluation.hpp` / `evaluation.cpp`: `sweep_all_images`; `parallel.hpp` for native task scheduling | `sweep-all` |
| `test_xormap_gray_fast.m` | `evaluation.cpp`: `verify_matlab_fast_path`; Catch2 translation and MATLAB fixtures under `tests/` | `verify` and CTest integration/unit tests |
| `xormap_image_decrypt.m` | `core.hpp` / `core.cpp`: `decrypt_canonical` | Library primitive and parity tests |
| `xormap_image_encrypt.m` | `core.hpp` / `core.cpp`: `encrypt_canonical` | Reference path and parity tests |
| `xormap_image_encrypt_fast.m` | `core.hpp` / `core.cpp`: `encrypt_fast`, including iteration reporting | Production evaluation path |
| `xormap_keystream.m` | `core.hpp` / `core.cpp`: `keystream_canonical` | Reference path and parity tests |
| `xormap_keystream_fast.m` | `core.hpp` / `core.cpp`: `keystream_fast` with a reusable `TransformPlan` | Production evaluation path |

The MATLAB project imports `xormap_transform.m` and
`xormap_transform_fast.m` from the sibling `xormap_matlab` project. Their
logic is also translated locally: `transform_canonical`, `make_transform_plan`,
and `transform_fast` in `core.cpp`. Consequently, the C++ project has no
runtime dependency on either MATLAB source directory; the sibling image
directory is only the default data location.

## Supporting native modules

| Module | Responsibility |
|---|---|
| `image.hpp` / `image.cpp` | Validated row-major grayscale image type, TIFF orientation/photometric normalization, TIFF writing, RFC 4180 CSV fields, and manifest parsing |
| `metrics.hpp` / `metrics.cpp` | Cipher metrics, exact MATLAB Twister stream, and adjacent-pair sampling overloads for both MATLAB generators |
| `threefry.hpp` / `threefry.cpp` | Exact MATLAB worker `threefry4x64_20` stream, full-precision `rand` conversion, `randi` mapping, and worker-side demo key |
| `parallel.hpp` | Dynamically scheduled `std::thread` worker pool with exception propagation |
| `download.hpp` / `download.cpp` | USC-SIPI inventory, thread-safe libcurl transfer, and validation-before-publish |
| `plot.hpp` / `plot.cpp` | Native Cairo vector-PDF plots, grayscale panels, histograms, scatter panels, legends, and reference lines |
| `apps/xormap_gray.cpp` | CLI parsing, runtime project discovery, option validation, and command dispatch |
| `tests/fixtures/matlab_vectors.hpp` | Fixed vectors generated from unmodified MATLAB R2026a sources |
| `tests/test_*.cpp` | Catch2 unit/integration tests for cipher and MATLAB-RNG parity, metrics, TIFF/manifest/download behavior, native parallel scheduling, all evaluation workflows, CSV readers/comparison, and PDF generation |

## Artifact parity and native additions

The C++ workflows generate the same CSV schemas and vector-PDF report families
as the MATLAB scripts. `run-all` also writes the plain, cipher, and recovered
TIFFs plus the underlying histogram and sampled-pair CSVs, making every plotted
value independently inspectable. `compare` adds a compact per-K aggregate CSV
beside its three MATLAB-equivalent PDFs.

All figures use Cairo's PDF surface and all task scheduling uses the native
`std::thread` pool. No MATLAB, Python, GUI process, or external job runner is
needed.
