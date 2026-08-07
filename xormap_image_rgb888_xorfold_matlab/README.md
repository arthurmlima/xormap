# xormap RGB888 cipher: one XOR-folded state per pixel, every SIPI color image

The RGB888 (24-bit) counterpart to
[xormap_image_rgb565_xorfold_matlab](../xormap_image_rgb565_xorfold_matlab)
— same one-state-per-pixel XOR-fold strategy, run across **every color
image the USC-SIPI database currently distributes** (51 images, ~35.5M
pixels; see [xormap_image_rgb888_matlab](../xormap_image_rgb888_matlab)'s
README for the full volume/size breakdown and why 24-bit needs no lossy
channel truncation, unlike RGB565). It is deliberately separate from that
non-fold sibling, so results from the two mappings are not mixed together.

## What changes

The non-fold implementation concatenates every transformed `K`-bit state
and slices that stream into 24-bit words: one PRNG iteration supplies
`K/24` pixels (16 pixels at K=384, this project's largest K).

This variant advances the PRNG **once per image pixel** and XOR-folds the
entire transformed state into exactly one 24-bit value. With state bits
numbered from zero (bit 0 is the LSB), folded output bit `b` is

```text
fold(state)[b] = state[b] XOR state[b+24] XOR state[b+48] XOR ...
```

for `b = 0,...,23` (terms at indices >= K are omitted). Equivalently:

```text
s_i = xormap_transform(s_(i-1), K)
w_i = xor_fold24(s_i)
c_i = p_i XOR w_i
```

An image with `N = rows * columns` RGB888 pixels always requires exactly
`N` xormap iterations for encryption and `N` for decryption — the count
does not shrink as `K` grows, which is why this is the most expensive
experiment in the whole project: at `K = 24:24:384` (16 values) x 2
encryptions (the second is for NPCR/UACI) x 51 images (~35.5M pixels),
that's on the order of **1.1 billion** state transitions.

As in the sibling image experiments, encryption derives the initial state
from the external key and a hash of the packed plaintext:

```text
seed = key_bits XOR hash_expand_bits(bytes-of(plain_RGB888), K)
```

Decryption is the same XOR operation with the keystream regenerated from
that seed — a round-trip check, not a complete key/IV transport protocol.

## Making 1.1 billion iterations tractable: fast transform + parallelism

Two multiplicative speedups make this feasible:

1. **O(K) fast transform.** `xor_fold24`/`xormap_keystream_xorfold24` are
   built on `xormap_transform_fast` (reused from
   [xormap_matlab](../xormap_matlab) rather than duplicated), a prefix-XOR
   reformulation of the canonical O(K²) `xormap_transform` that is
   sequence-equivalent to it by construction. `test_xormap_rgb888_xorfold.m`
   proves the equivalence
   per-K (every single-bit basis state) and additionally checks the fold
   itself against an independent, intentionally-slow reference
   implementation that never calls `xor_fold24`.
2. **Parallelism sized to the machine.** `sweep_k_xorfold_rgb888.m` splits
   the sweep into one task per **(image, K) pair** (51 x 16 = 816 tasks)
   run on a local `parpool` sized to `nproc` (the machine's logical core
   count, not MATLAB's `feature('numcores')` which only counts physical
   cores — 24 vs 12 on the machine this was last run on; the local cluster
   profile's `NumWorkers` is bumped up automatically if needed). Splitting
   by *(image, K)* rather than by image matters here specifically: the
   `wash-ir` image alone (2250x2250 = ~5.06M pixels) is about 14% of the
   entire database's pixel count, and its 16 K values would strand one
   worker for a very long time if they weren't spread across the pool.

Measured on a smaller representative workload (24 keystream generations
at K=384, 50000 pixels each) before committing to the full sweep: **9.18x
speedup** at 24 workers vs. serial. Combined with the fast transform, this
turns an estimated 3+ hour serial run into one on the order of tens of
minutes.

As with the non-fold sibling, each `parfor` task seeds `rng` from its own
task index before adjacent-pixel-correlation sampling, so results are
reproducible despite `parfor`'s non-deterministic task-to-worker
scheduling.

## Files

- `xor_fold24.m` performs the LSB-first 24-bit fold.
- `xormap_keystream_xorfold24.m` advances and folds once per requested
  pixel (inlines the fast transform and fold in its hot loop, same
  pattern as the RGB565 sibling's `xormap_keystream_xorfold16.m`).
- `xormap_rgb888_xorfold_encrypt.m` / `xormap_rgb888_xorfold_decrypt.m`
  implement the RGB888 round trip.
- `sweep_k_xorfold_rgb888.m` runs the parallelized K=24:24:384 sweep over
  every SIPI color image.
- `analysis_xorfold_rgb888.m` runs the key-sensitivity, histogram
  chi-square, and PSNR test battery (same three tests as
  [xormap_image_rgb888_matlab](../xormap_image_rgb888_matlab)'s
  `analysis_rgb888.m`) against this fold cipher.
- `compare_xorfold_vs_std_rgb888.m` plots this sweep against the non-fold
  sibling's sweep, metric by metric.
- `test_xormap_rgb888_xorfold.m` is a deterministic correctness test.

Reused rather than duplicated from sibling folders:
`../xormap_matlab` (canonical `xormap_transform`, `xormap_fast_plan`,
`xormap_transform_fast`), `../xormap_image_matlab` (hashing, keys,
metrics), and `../xormap_image_rgb888_matlab` (`rgb888_pack`/
`rgb888_unpack`, `words24_to_bytes_be`, the shared `images/` download +
manifest).

## Metrics

Same as the non-fold sibling: mean entropy and mean |horizontal
correlation| across the three (standard 8-bit) R/G/B channels, plus
NPCR/UACI on the packed 24-bit word (L=2^24: ideal NPCR≈100.0000%,
UACI≈33.3333%).

## Results

See `results/sweep_k_xorfold_rgb888.csv` (816 rows) and
`results/sweep_k_xorfold_rgb888.png` / `.pdf` — light gray points are
individual (image, K) measurements, the bold line is the mean across all
51 images.

Across the 816 (image, K) cases: mean per-channel entropy 7.990523--7.999968
bits, mean |horizontal correlation| 0.001576--0.051015, packed-word NPCR
99.999619--100.0000%, packed-word UACI 32.157--34.324%. Two-encryption time
ranged 1.68s (K=24, smallest images) to 337.6s (K=384, largest images,
mostly the 2250x2250 `wash-ir`); the full 816-task sweep completed in 23.6
minutes on a 24-core machine.

## Key sensitivity, histogram and PSNR analysis (`analysis_xorfold_rgb888.m`)

The XOR-fold counterpart of `analysis_rgb888.m` in the non-fold sibling
[xormap_image_rgb888_matlab](../xormap_image_rgb888_matlab) (itself the
colour counterpart of [xormap_image_matlab](../xormap_image_matlab)'s
`analysis_gray.m`): the same three tests, run against this fold cipher
across all 51 SIPI colour images at `K = 24:24:384` (816 (image, K)
tasks — the same scale as the sweep above, since every xorfold encryption
is O(N) regardless of K — plus a per-bit-position study on the smallest
image, 4.1.01, 256x256, chosen explicitly to bound that study's cost).

```matlab
analysis_xorfold_rgb888
```

Writes three CSV + vector PDF pairs into `results/`:

- `key_sensitivity_xorfold_rgb888.{csv,pdf}` — flips one key bit and
  measures NPCR/UACI/PSNR (on the packed 24-bit word) between the two
  resulting ciphers, plus PSNR of a wrong-key decryption, plus a per-bit
  study. Because both `xormap_transform` and the fold are linear over
  GF(2), the ciphertext difference `C1 XOR C2` should depend only on
  `key1 XOR key2`, not on the image — confirmed at all 16 of 16 tested K
  values (every one of the 51 images produced the identical
  `key_diff_checksum` at a given K).
- `histogram_analysis_xorfold_rgb888.{csv,pdf}` — chi-square uniformity of
  the cipher's R/G/B histograms (8-bit channels, dof=255 as in the
  non-fold sibling). 765/816 cases pass at alpha=0.05, and the 51
  failures are not scattered noise: **all 51 occur at K=24**, the very
  bottom of this sweep's range (0/51 pass there; every K >= 48 is 51/51).
  This matches the "small-K danger zone" the non-fold ciphers already
  documented at their own bottom-of-range K — here it lands exactly at
  the fold's minimum valid K (`xor_fold24` requires K a multiple of 24).
- `psnr_analysis_xorfold_rgb888.{csv,pdf}` — PSNR(plain, cipher) and the
  round-trip PSNR. Plain-vs-cipher and plain-vs-wrong-key PSNR both land
  around 6.2-10.0 dB (low is good here); round-trip PSNR was exactly +Inf
  in all 816 cases (lossless XOR cipher, asserted as a correctness check).

`results/all_tests_xorfold_rgb888.pdf` concatenates all seven vector PDFs
(sweep, the three analysis tests above, and the three XOR-fold-vs-standard
comparison figures below) into one document.

## XOR-fold vs standard: side-by-side comparison (`compare_xorfold_vs_std_rgb888.m`)

Plots this sweep against the non-fold sibling's `sweep_k_rgb888.m` sweep
for every metric they share (entropy, correlation, NPCR, UACI), identical
axis limits on both panels of each figure so panel-to-panel differences
are real. Requires both sweeps' CSVs to already exist.

```matlab
compare_xorfold_vs_std_rgb888
```

Writes `results/comparison/compare_entropy.pdf`,
`compare_correlation.pdf` and `compare_npcr_uaci.pdf`.

## Reproducing

```matlab
cd('../xormap_image_rgb888_matlab')
download_images
cd('../xormap_image_rgb888_xorfold_matlab')
sweep_k_xorfold_rgb888
analysis_xorfold_rgb888
compare_xorfold_vs_std_rgb888   % needs ../xormap_image_rgb888_matlab/sweep_k_rgb888.m's CSV too
```

```matlab
test_xormap_rgb888_xorfold  % correctness: fast==canonical, known fold, round-trip, iteration count
```

These distributional image tests are not a security proof. The xormap
transform and the fold are both linear over GF(2), so the construction
should not be treated as a production cryptosystem — see the key-sensitivity
result above, which demonstrates that linearity directly rather than just
asserting it.
