# xormap RGB888 image cipher: every USC-SIPI color image

The RGB888 (24 bits/pixel) counterpart to
[xormap_image_rgb565_matlab](../xormap_image_rgb565_matlab) — same
`K/word_width`-pixels-per-iteration cipher strategy, but run across
**every color image the USC-SIPI database currently distributes**, not
just a 3-image sample. See also
[xormap_image_matlab](../xormap_image_matlab), the grayscale counterpart
run across every SIPI grayscale image on the same K grid.

## USC-SIPI has 24-bit images natively — no truncation needed

Unlike RGB565 (which needed lossy 8-to-5/6-bit channel truncation), SIPI's
color images are already standard 24-bit truecolor (8 bits/channel,
confirmed via `imfinfo`), so `rgb888_pack.m` just repacks the three uint8
channels into one uint32 word (`bits[23:16]=R, [15:8]=G, [7:0]=B`) — no
information is lost.

## Every color image in the database

Textures and Sequences are entirely grayscale/mono. Only Miscellaneous and
Aerials contribute color images, 51 in total (`download_images.m`,
`images/manifest.csv`):

| Volume | Color images | Sizes | Total pixels |
|---|---|---|---|
| misc | 14 | 8×256², 6×512² | ~2.1M |
| aerials | 37 | 12×512², 24×1024², 1×2250² (`wash-ir`) | ~33.4M |
| **Total** | **51** | | **~35.5M** |

(SIPI no longer distributes Lena/4.2.04 or Tiffany/4.2.02, same as noted
in the grayscale README.) The 24 1024×1024 images plus the single
2250×2250 `wash-ir` image account for ~94% of total pixel count — that
dominates the runtime discussion below.

## The cipher

`xormap_rgb888_encrypt.m` mirrors `xormap_rgb565_encrypt.m` at 24
bits/pixel instead of 16:

```
seed = key_bits XOR hash_expand_bits(bytes-of(plain_packed), K)
```

`xormap_keystream_words_fast.m` iterates `xormap_transform` from `seed`
and packs its output into 24-bit words. **Each iteration produces K/24
pixels' worth of keystream on average** — at K=384 (the top of this
project's sweep) that's exactly 16 pixels/iteration, and at K=24 (the
bottom) exactly 1 pixel/iteration.

### Why this one uses the fast transform, unlike its RGB565 sibling

`xormap_image_rgb565_matlab` computes each state update with the
canonical `xormap_transform` (`../xormap_matlab`), which is O(K²) per
call. That's fine for 3 images. Run across all 51 SIPI color images at
K=24:24:384, it would take on the order of **3+ hours**. This pipeline
instead uses `xormap_transform_fast` (from
[xormap_matlab](../xormap_matlab), alongside the canonical transform it
reformulates), an O(K) prefix-XOR reformulation that is
sequence-equivalent to `xormap_transform` by construction — proven
per-K in `test_xormap_rgb888.m` by checking every single-bit basis state,
and independently by diffing its output byte-for-byte against the
existing (naive) `xormap_keystream_words` from `xormap_image_rgb565_matlab`.

### Parallelized across the local machine's cores

Even with the fast transform, sweeping 51 images (up to 2250×2250) over
16 K values is substantial. `sweep_k_rgb888.m` splits the work into one
task per **(image, K) pair** — 51 × 16 = 816 independent tasks — and runs
them on a 24-worker local `parpool` (this machine has a 24-thread i9;
the "Processes" cluster profile's `NumWorkers` is bumped from its default
12 to 24 automatically if needed). Splitting by *(image, K)* rather than
just by image matters: it spreads even the giant `wash-ir` image's 16 K
values across many workers, instead of one worker being stuck running
all 16 serially while everyone else idles.

Because `adjacent_correlation` samples randomly, each task seeds `rng`
deterministically from its own task index before sampling, so results are
reproducible run-to-run despite `parfor`'s non-deterministic task-to-worker
scheduling.

## Metrics (`sweep_k_rgb888.m`)

All three RGB888 channels are standard 8-bit, so metrics use the same
ideals as the grayscale pipeline (unlike RGB565's non-uniform 5/6/5
channels, which needed per-channel ideals):

- **Entropy** (mean of `shannon_entropy` across R/G/B, default `nbits=8`):
  ideal 8.0 bits.
- **Adjacent-pixel correlation** (mean of `abs(adjacent_correlation(...,
  'horizontal', ...))` across R/G/B).
- **NPCR/UACI** (`npcr_uaci.m` + `npcr_uaci_ideal.m`) on the packed
  24-bit word (L=2^24): ideal NPCR≈100.0000%, UACI≈33.3333%.

## Results

See `results/sweep_k_rgb888.csv` (816 rows: one per image×K) and
`results/sweep_k_rgb888.pdf` — light gray points are individual
(image, K) measurements, the bold line is the mean across all 51 images
at each K.

<!-- RESULTS_PLACEHOLDER -->

## Reproducing

```matlab
download_images    % fetches all 51 images/*.tiff from sipi.usc.edu (not tracked in git)
sweep_k_rgb888      % encrypts every image at every K=24:24:384, writes results/
```

```matlab
test_xormap_rgb888  % correctness: fast==naive keystream, pack/unpack, round-trip
```
