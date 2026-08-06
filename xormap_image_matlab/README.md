# xormap image cipher: USC-SIPI evaluation

Uses [xormap_matlab](../xormap_matlab)'s `xormap_transform` as the keystream
generator for a stream cipher, and runs the standard test battery used to
evaluate image ciphers in the literature: histogram, adjacent-pixel
correlation, entropy, and NPCR/UACI.

See also [xormap_image_rgb565_matlab](../xormap_image_rgb565_matlab), the
same strategy applied to RGB565-packed color images.

## The cipher

`xormap_image_encrypt.m` derives a **plaintext-related** K-bit initial
condition:

```
seed = key_bits XOR hash_expand_bits(plain_image_bytes, K)
```

`K` is `numel(key_bits)` — any value works (`K > 4`, multiple of 8), it
isn't tied to SHA-256's own 256-bit width. `hash_expand_bits.m` is a small
counter-mode KDF (concatenate `SHA256([bytes, counter])` blocks until there
are `K` bits, then truncate) that expands or truncates a SHA-256 digest to
exactly `K` bits. `key_bits` is a fixed "external" secret (`secret_key.m` —
in a real deployment this would be pre-shared and kept off the image
entirely; here it's a fixed PRNG pattern so encrypt/decrypt agree without
passing a key by hand).

`xormap_keystream.m` then iterates `xormap_transform` from `seed` exactly
the way `Genv_xormap.m`'s hardware register advances on every `en` pulse
(`x_reg <= xormap_transform(x_reg)`), concatenating each K-bit output into a
byte stream, which is XORed against the image. **Each iteration produces
K/8 pixels' worth of keystream** (8 bits/pixel, grayscale) — at the default
K=512 that's 64 pixels/iteration.

Seeding from the image's own hash (rather than a fixed IV) means a single
bit changed anywhere in the plaintext produces a completely unrelated seed
(hash avalanche) and therefore a completely different keystream — that
sensitivity is exactly what the NPCR/UACI test below measures.
`xormap_image_decrypt.m` regenerates the same keystream from a given seed
and XORs it back off (XOR is self-inverse); `run_all.m` uses it as a
round-trip correctness check, not as a full protocol (a real receiver would
need `seed` delivered separately, since it depends on the plaintext).

## Images

Three natively-grayscale images from the USC-SIPI Miscellaneous volume
(`download_images.m`):

| File | Description | Size |
|---|---|---|
| boat.512 | Fishing boat | 512x512 |
| 5.1.09 | Moon surface | 256x256 |
| 7.1.01 | Truck | 512x512 |

SIPI's Peppers image (4.2.07) is only distributed in color, so 7.1.01
(Truck) is used in its place — every test image here is native 8-bit
grayscale, none are RGB-converted.

## Metrics (`run_all.m`)

- **Histogram** (`plot_histogram`, a local function in `run_all.m`) — a
  good cipher's histogram is flat; the plain image's is not.
- **Adjacent-pixel correlation** (`adjacent_correlation.m`) — Pearson
  correlation between horizontal/vertical/diagonal neighbour pairs, sampled
  randomly. Near 1 for a plain image, near 0 for a well-scrambled cipher.
- **Entropy** (`shannon_entropy.m`) — Shannon entropy in bits; ideal for an
  8-bit cipher image is 8.0.
- **NPCR/UACI** (`npcr_uaci.m`, ideal values from `npcr_uaci_ideal.m`) —
  encrypts the plain image and a copy with the centre pixel's LSB flipped,
  and compares the two cipher images. Ideal for 8-bit (L=256 level) images:
  NPCR = 100(L-1)/L = 99.6094%, UACI = 100(L+1)/(3L) = 33.4635%.

## Results (last run, K=512)

| Image | Entropy (plain) | Entropy (cipher) | Corr-H (plain) | Corr-H (cipher) | Corr-V (plain) | Corr-V (cipher) | Corr-D (plain) | Corr-D (cipher) | NPCR % | UACI % |
|---|---|---|---|---|---|---|---|---|---|---|
| boat.512 (Fishing Boat, 512x512) | 7.1914 | 7.9994 | +0.9408 | +0.0191 | +0.9698 | -0.0036 | +0.9205 | +0.0083 | 99.6094 | 33.4909 |
| 5.1.09 (Moon Surface, 256x256) | 6.7093 | 7.9973 | +0.9045 | -0.0181 | +0.9458 | +0.0009 | +0.9015 | +0.0017 | 99.5743 | 33.3923 |
| 7.1.01 (Truck, 512x512) | 6.0274 | 7.9992 | +0.9625 | +0.0046 | +0.9302 | -0.0002 | +0.9059 | -0.0096 | 99.6071 | 33.5379 |

Round-trip (`decrypt(encrypt(P)) == P`) passes for all three images. Per
image, `results/<name>_images.png`, `_histogram.png` and `_correlation.png`
show plain vs. cipher side by side.

## Does a larger K make the cipher better? (`sweep_k.m`)

`sweep_k.m` sweeps `K = 8:4:512` (every 4 bits, from the smallest K
`xormap_transform` accepts up to 512 — 127 values) across all three images
and re-measures cipher entropy, adjacent-pixel correlation, NPCR and UACI
at each K. Result (`results/sweep_k.png`, `results/sweep_k.csv`): **mostly
no, with one sharp exception at the very bottom of the range.**

At **K=8** every metric is visibly degraded and consistent across all
three images — entropy drops to 6.7-7.8 (vs. ~8.0), horizontal correlation
jumps to 0.29-0.76 (vs. ~0.02), UACI drifts to 33-41% (vs. ~33.46% ideal).
From **K=12 on**, every metric is already back to saturated-near-ideal and
stays flat (within sampling noise) all the way to K=512 — confirming the
original 32-step sweep's finding, just now with visibility into the part
it never sampled. K=8 is one `xormap_keystream` step above the hard
minimum (`xormap_transform` requires K>4), so this reads as a genuine
"too little state to mix well" effect, not noise — worth remembering if
you're tempted to run this cipher at a very small K. (K=9, 10, 11 weren't
tested — the step is 4 — so exactly where between 8 and 12 it recovers is
unknown; treat anything below ~16 as unvalidated.)

What *does* keep scaling with K, across the whole range: wall-clock cost.
Above K~40 it grows roughly linearly (more inner XOR work per
`xormap_transform` call). Below that, cost rises again toward K=8 for the
opposite reason — `xormap_keystream` needs to *call* `xormap_transform`
many more times to accumulate the same number of output bits from a
narrower register, and that per-call loop overhead dominates at the small
end. The time-vs-K curve is a shallow "hook" (minimum around K~15-40), not
a monotonic line.

Caveat: these four tests measure statistical/distributional quality, not
cryptographic strength. `xormap_transform` is a purely linear (XOR-only)
map over GF(2), so in principle a large enough set of known plaintext/
ciphertext pairs could set up and solve a linear system for the keystream
at any K above the K=8 danger zone — a bigger K raises the number of
unknowns in that system, but these tests wouldn't detect the difference
either way. Take the "K doesn't matter above ~12" finding as being about
histogram/correlation/entropy/NPCR/UACI specifically, not as a security
proof.

## Reproducing

```matlab
download_images   % fetches images/*.tiff from sipi.usc.edu (not tracked in git)
run_all           % encrypts (K=512), checks round-trip, computes metrics, writes results/
sweep_k           % re-runs the metrics for K=8:4:512, writes results/sweep_k.*
```

Runtime: `run_all` takes a few seconds (`xormap_transform` at K=256 runs in
~0.15ms/call, benchmarked separately; cost scales with K, see above).
`sweep_k` takes on the order of 5-10 minutes (127 K values x 3 images x 2
encryptions each; ran in the background last time, ~7 minutes wall-clock).
