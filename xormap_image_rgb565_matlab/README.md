# xormap RGB565 image cipher: USC-SIPI evaluation

The RGB565 (16 bits/pixel) counterpart to
[xormap_image_matlab](../xormap_image_matlab) (8-bit grayscale) — same
cipher strategy, same test battery, applied to packed RGB565 color images
matching this project's `tangprimer25k_st7789_top` target (the ST7789 is a
16bpp RGB565 TFT controller).

## USC-SIPI doesn't have RGB565 images

Nobody distributes images in RGB565 — it's a packed hardware pixel format,
not a file format. SIPI's color images are standard 24-bit truecolor TIFF
(8 bits/channel; confirmed via `imfinfo`). `download_images.m` fetches
those, and `rgb888_to_rgb565.m` converts each down to a packed `uint16`
RGB565 image: `bits[15:11]=R5, [10:5]=G6, [4:0]=B5` (truncating, not
rounding, each channel — the standard ST7789-style layout).
`rgb565_to_channels.m` splits a packed image back into R5/G6/B5 component
matrices for per-channel metrics; `rgb565_to_rgb888_preview.m` expands
those back to 8-bit via bit replication (`R8=(R5<<3)|(R5>>2)`, so 0 stays 0
and the max value stays 255) purely for the plain/cipher preview figures —
the cipher itself only ever operates on the packed 16-bit words.

## Images

| File | Description | Size |
|---|---|---|
| 4.2.03 | Mandrill (Baboon) | 512x512 color |
| 4.2.05 | Airplane (F-16) | 512x512 color |
| 4.2.07 | Peppers | 512x512 color |

Chosen in the same spirit as the grayscale set: object/animal subjects,
not the personal-photo NTSC test stills SIPI also hosts (4.1.01-4.1.04).

## The cipher

`xormap_rgb565_encrypt.m` mirrors `xormap_image_matlab/xormap_image_encrypt.m`
at 16 bits/pixel instead of 8:

```
seed = key_bits XOR hash_expand_bits(bytes-of(plain_packed), K)
```

`xormap_keystream_words.m` iterates `xormap_transform` from `seed` and
packs its output into 16-bit words (`bits_to_words.m`, the word-width
generalization of the grayscale pipeline's `bits_to_bytes.m`) instead of
bytes. **Each iteration produces K/16 pixels' worth of keystream** — at the
default K=512 that's 32 pixels/iteration (half the grayscale rate, since
each RGB565 pixel is twice as wide). `xormap_rgb565_decrypt.m` is the
inverse, same round-trip-only caveat as the grayscale version.

## Metrics (`run_all_rgb565.m`)

Same four tests as the grayscale pipeline, computed **per channel** (R:5
bits, G:6 bits, B:5 bits) since RGB565's channels aren't 8 bits wide, plus
NPCR/UACI on the full packed 16-bit word:

- **Histogram**, **adjacent-pixel correlation**: as in the grayscale
  pipeline, per channel (`rgb565_to_channels.m` + the same
  `adjacent_correlation.m`).
- **Entropy** (`shannon_entropy.m(channel, nbits)`): ideal is 5.0 bits for
  R/B, 6.0 bits for G (their own `nbits`, not 8).
- **NPCR/UACI** (`npcr_uaci.m` + `npcr_uaci_ideal.m`): computed once on the
  packed word (L=65536: ideal NPCR=99.9985%, UACI=33.3338% — **not** the
  commonly-quoted 8-bit 99.6094%/33.4635%, which don't apply at a different
  bit depth) and once per channel (L=32 or 64: ideal NPCR=96.8750%/
  98.4375%, UACI=34.3750%/33.8542%).

## Results (last run, K=512)

### Entropy and NPCR/UACI (packed word, ideal 99.9985%/33.3338%)

| Image | Entropy R (plain) | Entropy R (cipher) | Entropy G (plain) | Entropy G (cipher) | Entropy B (plain) | Entropy B (cipher) | NPCR % | UACI % |
|---|---|---|---|---|---|---|---|---|
| Mandrill | 4.7163 | 4.9999 | 5.4778 | 5.9998 | 4.7594 | 4.9999 | 99.9966 | 33.3651 |
| Airplane F-16 | 3.7683 | 4.9999 | 4.8238 | 5.9998 | 3.2785 | 4.9999 | 99.9973 | 33.3470 |
| Peppers | 4.3496 | 4.9999 | 5.5813 | 5.9998 | 4.1847 | 4.9999 | 99.9996 | 33.3568 |

### NPCR/UACI, per channel (ideal R/B 96.8750%/34.3750%, G 98.4375%/33.8542%)

| Image | NPCR R % | UACI R % | NPCR G % | UACI G % | NPCR B % | UACI B % |
|---|---|---|---|---|---|---|
| Mandrill | 96.8632 | 34.4061 | 98.4112 | 33.8569 | 96.8830 | 34.2928 |
| Airplane F-16 | 96.8555 | 34.3880 | 98.4440 | 33.8810 | 96.8761 | 34.3858 |
| Peppers | 96.8678 | 34.3997 | 98.4653 | 33.8965 | 96.8502 | 34.3737 |

Round-trip (`decrypt(encrypt(P)) == P`) passes for all three images. Per
image, `results/<name>_images.png` (color preview, plain vs. cipher),
`_histogram.png` and `_correlation.png` (both 2x3: rows plain/cipher,
columns R/G/B) show the full picture; `results/results.md` has the
horizontal-correlation table.

## Does a larger K make the cipher better? (`sweep_k_rgb565.m`)

Same sweep as the grayscale pipeline, `K = 8:4:512` (127 values), tracking
mean normalized entropy (each channel's entropy / its own max) and mean
|horizontal correlation| across channels, plus packed-word NPCR/UACI.
Same finding as grayscale, including the same exception
(`results/sweep_k_rgb565.png`, `results/sweep_k_rgb565.csv`): **K=8 is
visibly degraded** on every image — normalized entropy dips to 0.91-0.99
(vs. ~1.0), mean |horizontal correlation| spikes to 0.13-0.43 (vs. ~0.02),
packed-word UACI swings to 22-43% (vs. ~33.33% ideal). From **K=12 on**,
everything is flat and near-ideal out to K=512, and encrypt time resumes
its roughly-linear growth with K (with the same small-K "hook" as
grayscale — more `xormap_transform` calls needed to fill a narrower
register costs more than the wider per-call math saves). See the
grayscale README's section for the full explanation and the caveat about
what these four tests do and don't measure — both apply here unchanged.

## Reproducing

```matlab
download_images     % fetches images/*.tiff from sipi.usc.edu (not tracked in git)
run_all_rgb565      % converts to RGB565, encrypts (K=512), checks round-trip, computes metrics, writes results/
sweep_k_rgb565      % re-runs the metrics for K=8:4:512, writes results/sweep_k_rgb565.*
```

Runtime: `sweep_k_rgb565` takes on the order of 15-20 minutes (127 K
values x 3 images x 2 encryptions each, at 16 bits/pixel; ran in the
background last time alongside the grayscale sweep).
