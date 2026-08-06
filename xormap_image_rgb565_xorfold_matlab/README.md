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
- `xormap_fast_plan.m` builds a reusable transform plan for one `K`.
- `xormap_transform_fast.m` evaluates that plan without rebuilding it for
  every pixel.
- `xormap_keystream_xorfold16.m` advances and folds once per requested pixel.
- `xormap_rgb565_xorfold_encrypt.m` and
  `xormap_rgb565_xorfold_decrypt.m` implement the RGB565 round trip.
- `sweep_k_xorfold_rgb565.m` runs the requested `K = 32:32:512` image sweep.
- `test_xormap_rgb565_xorfold.m` is a small, deterministic correctness test.

The implementation reuses code from these sibling folders:

- `../xormap_matlab` for the canonical `xormap_transform` reference;
- `../xormap_image_matlab` for hashing, keys, and image-cipher metrics;
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

The sweep prints per-image/per-`K` progress and checks that the reported
iteration count equals the image's pixel count. It writes:

- `results/sweep_k_xorfold_rgb565.csv` — measurements including `K`, pixel and
  iteration counts, statistical metrics, and elapsed seconds;
- `results/sweep_k_xorfold_rgb565.png` — metrics and runtime plotted over `K`.

## Last completed sweep

The three 512x512 images produced 48 rows (3 images x 16 K values), with all
iteration invariants passing:

- `image_size_pixels = iterations_per_encrypt = 262144`;
- `total_iterations_two_encryptions = 524288` (the second encryption is for
  NPCR/UACI);
- `pixels_per_iteration = 1` for every K;
- `fold_chunks = K/16`, from 2 chunks at K=32 to 32 chunks at K=512.

Across those 48 cases, mean normalized channel entropy was
0.999974--0.999985, packed-word NPCR was 99.996948--99.999619%, and
packed-word UACI was 33.214720--33.447761%. Two-encryption time averaged
3.02 seconds at K=32 and 10.86 seconds at K=512 on the machine used for the
run. See the CSV for every measured value.

Runtime is expected to be substantially higher than the existing
`K/16`-pixels-per-iteration scheme: every encryption now performs `N`
transformations instead of `ceil(16*N/K)`. The CSV's timing column records the
actual runtime on the current machine; it is more useful than a fixed estimate
because MATLAB version, CPU, image size, and `K` all affect it.

These distributional image tests are not a security proof. The xormap
transform and the fold are both linear over GF(2), so the construction should
not be treated as a production cryptosystem.
