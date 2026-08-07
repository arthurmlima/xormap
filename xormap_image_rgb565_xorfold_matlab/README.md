# xormap RGB565 cipher: one XOR-folded state per pixel

This folder tests a different way of turning each xormap PRNG state into an
RGB565 keystream word. It is deliberately separate from
[`xormap_image_rgb565_matlab`](../xormap_image_rgb565_matlab), so results from
the two mappings are not mixed together.

## What changes

The existing RGB565 implementation concatenates every transformed `K`-bit
state and slices that stream into 16-bit words. One PRNG iteration therefore
supplies `K/16` pixels (for example, 32 pixels when `K=512`).

This variant advances the PRNG **once per image pixel** and XOR-folds the
entire transformed state into exactly one 16-bit word. With state bits numbered
from zero (bit 0 is the LSB), folded output bit `b` is

```text
fold(state)[b] = state[b] XOR state[b+16] XOR state[b+32] XOR ...
```

where terms at indices greater than or equal to `K` are omitted, for
`b = 0,...,15`. Equivalently,

```text
s_i = xormap_transform(s_(i-1), K)
w_i = xor_fold16(s_i)
c_i = p_i XOR w_i
```

Thus an image containing `N = rows * columns` RGB565 pixels always requires
exactly `N` xormap iterations for encryption and `N` for decryption. The count
does not shrink as `K` grows. For this test's `K = 32:32:512`, each folded bit
combines between 2 and 32 state bits.

As in the sibling image experiments, encryption derives the initial state from
the external key and a hash of the packed plaintext:

```text
seed = key_bits XOR hash_expand_bits(bytes-of(plain_RGB565), K)
```

Decryption is the same XOR operation with the keystream regenerated from that
seed. The supplied seed is therefore part of this round-trip experiment, not a
complete key/IV transport protocol.

## Files

- `xor_fold16.m` performs the LSB-first fold.
- `xormap_keystream_xorfold16.m` advances and folds once per requested pixel.
- `xormap_rgb565_xorfold_encrypt.m` and
  `xormap_rgb565_xorfold_decrypt.m` implement the RGB565 round trip.
- `sweep_k_xorfold_rgb565.m` runs the requested `K = 32:32:512` image sweep.
- `analysis_xorfold_rgb565.m` runs the key-sensitivity, histogram
  chi-square, and PSNR test battery (same three tests as
  [xormap_image_matlab](../xormap_image_matlab)'s `analysis_gray.m`)
  against this fold cipher.
- `test_xormap_rgb565_xorfold.m` is a small, deterministic correctness test.

The implementation reuses code from these sibling folders:

- `../xormap_matlab` for the canonical `xormap_transform`, plus
  `xormap_fast_plan`/`xormap_transform_fast` (the O(K) reformulation used
  in every hot loop here);
- `../xormap_image_matlab` for hashing, keys, and image-cipher metrics
  (including `chi_square_uniformity.m` and `psnr_db.m`);
- `../xormap_image_rgb565_matlab` for RGB565 conversion and the USC-SIPI
  image download.

MATLAB's Java runtime is used by the existing SHA-256 helper. The test itself
uses only a small synthetic `uint16` image and requires no downloaded data.

## Run the regression test

From this folder in MATLAB:

```matlab
test_xormap_rgb565_xorfold
```

The test checks the known fold `0x1234 XOR 0x5678 = 0x444C`, compares the fast
transform with the canonical transform for every single-bit basis state,
independently generates canonical folded words, and verifies
encryption/decryption plus the exact pixel-sized iteration count for every
`K = 32:32:512`.

## Run the image sweep

Download the three source images once using the sibling RGB565 project, then
run this variant:

```matlab
cd('../xormap_image_rgb565_matlab')
download_images
cd('../xormap_image_rgb565_xorfold_matlab')
sweep_k_xorfold_rgb565
```

The sweep splits the 3 images x 16 K values into 48 independent tasks on a
local `parpool` sized to this machine's logical core count (`nproc`, not
MATLAB's `feature('numcores')` which only counts physical cores), prints
per-task progress, and checks that the reported iteration count equals each
image's pixel count. It writes:

- `results/sweep_k_xorfold_rgb565.csv` — measurements including `K`, pixel and
  iteration counts, statistical metrics, and elapsed seconds;
- `results/sweep_k_xorfold_rgb565.png` and `.pdf` — metrics and runtime
  plotted over `K` (raster and vector versions of the same figure).

## Last completed sweep

The three 512x512 images produced 48 rows (3 images x 16 K values), with all
iteration invariants passing:

- `image_size_pixels = iterations_per_encrypt = 262144`;
- `total_iterations_two_encryptions = 524288` (the second encryption is for
  NPCR/UACI);
- `pixels_per_iteration = 1` for every K;
- `fold_chunks = K/16`, from 2 chunks at K=32 to 32 chunks at K=512.

Across those 48 cases, mean normalized channel entropy was
0.999971--0.999984, packed-word NPCR was 99.996948--99.999619%, and
packed-word UACI was 33.235519--33.445876%. On a 24-core machine with the
sweep parallelized across all 48 tasks, the whole sweep completed in 60.4s
(vs. minutes serially). See the CSV for every measured value.

## Key sensitivity, histogram and PSNR analysis (`analysis_xorfold_rgb565.m`)

The XOR-fold counterpart of
[xormap_image_matlab](../xormap_image_matlab)'s `analysis_gray.m` and
[xormap_image_rgb888_matlab](../xormap_image_rgb888_matlab)'s
`analysis_rgb888.m` — the same three tests, run against this fold cipher
instead of the K/16-pixels-per-iteration one, across the same 3 images at
`K = 32:32:512` (48 (image, K) tasks, plus a per-bit-position study on one
image). Because RGB565 channels are non-uniform bit depth (R=5, G=6, B=5,
unlike grayscale/RGB888's flat 8 bits), chi-square and PSNR are each
computed **per channel against that channel's own critical value / peak
value**, then averaged — see the docstring in `analysis_xorfold_rgb565.m`
for why a raw cross-channel average would not be meaningful here.

```matlab
analysis_xorfold_rgb565
```

Writes three CSV + vector PDF pairs into `results/`:

- `key_sensitivity_xorfold_rgb565.{csv,pdf}` — flips one key bit and
  measures NPCR/UACI/PSNR between the two resulting ciphers, plus PSNR of a
  wrong-key decryption, plus a per-bit-position study. Because both
  `xormap_transform` and the fold are linear over GF(2), the ciphertext
  difference `C1 XOR C2` should depend only on `key1 XOR key2`, not on the
  image — confirmed at all 16 of 16 tested K values (every image produced
  the identical `key_diff_checksum` at a given K).
- `histogram_analysis_xorfold_rgb565.{csv,pdf}` — chi-square uniformity of
  the cipher's R/G/B histograms. 47/48 (image, K) cases pass at
  alpha=0.05; the one failure is consistent with the test's own ~5%
  false-positive rate at that significance level, not a systematic effect
  (contrast the RGB888 fold sibling below, where the failures cluster
  entirely at the smallest K).
- `psnr_analysis_xorfold_rgb565.{csv,pdf}` — PSNR(plain, cipher) and the
  round-trip PSNR. Plain-vs-cipher and plain-vs-wrong-key PSNR both land
  around 7.5-8.6 dB (low is good here); round-trip PSNR was exactly +Inf
  in all 48 cases (lossless XOR cipher, asserted as a correctness check).

`results/all_tests_xorfold_rgb565.pdf` concatenates all four vector PDFs
(sweep + the three analysis tests above) into one document.

These distributional image tests are not a security proof. The xormap
transform and the fold are both linear over GF(2), so the construction should
not be treated as a production cryptosystem — see the key-sensitivity result
above, which demonstrates that linearity directly rather than just asserting
it.
