# Tikz — LaTeX/pgfplots reproductions of the sweep/analysis results

Every plot here is generated straight from the `sweep_k*.csv` files described
in the repository root [`README.md`](../README.md) — same data, same ideal
reference values (computed with the same formula as
`xormap_image::npcr_uaci_ideal` in the C++ source), same colour convention as
the native Cairo-rendered `sweep_k*.pdf` reports (orange for
entropy/correlation/plaintext-bit-flip NPCR, blue for everything key-related).

## Layout

```
Tikz/
  config.tex       one shared pgfplots style — load this once
  main.tex         a compilable document that \input's every plot below
  plots/*.tex       one tikzpicture per plot (21 total)
  data/*.csv        the data each plot reads, two files per plot:
                     <name>_points.csv — every individual (image, K) value (full data,
                                          used for the rendered scatter too)
                     <name>_means.csv  — mean over images at each K
```

## Usage

In your own document:

```latex
\input{Tikz/config.tex}   % once, in the preamble (or just before first use)
...
\begin{figure}
  \centering
  \input{Tikz/plots/gray_entropy.tex}
  \caption{Cipher entropy vs. K, grayscale corpus.}
\end{figure}
```

Or compile `Tikz/main.tex` directly to get every plot in one PDF:

```sh
export PATH="/home/arthur/texlive/2026/bin/x86_64-linux:$PATH"
cd Tikz && pdflatex main.tex
```

This environment had no LaTeX toolchain and no sudo access, so a full TeX
Live 2026 (`scheme-full`) was installed to `/home/arthur/texlive/2026`
under the user's own home directory — a normal, complete, unattended
`install-tl` run (`perl install-tl -profile texlive.profile`, `TEXDIR`
pointing under `$HOME`), no root needed. `xelatex`/`lualatex` work the
same way. (An earlier pass used
[tectonic](https://tectonic-typesetting.github.io/), a self-contained
engine with no separate install; it worked but has a fixed, non-tunable
TeX memory ceiling — see below — so it was replaced with a real TeX Live
once one could actually be installed.)

Each plot file references its data as `data/<file>.csv` — a path relative
to wherever compilation runs from (`Tikz/`, i.e. the directory containing
`main.tex`), **not** relative to the plot file's own location in `plots/`
— that's just how LaTeX resolves `\input`. Compile from inside `Tikz/`, or
from the repo root, keeping `config.tex`, `plots/`, and `data/` together.

### A note on TeX engine memory

Plotting the complete per-image scatter (2544 points for grayscale) across
many large figures in one 21-figure document needs more memory than
stock TeX ships with: the classic web2c default is `main_memory =
5000000`, and this document exceeds it. Real TeX Live makes that tunable
(unlike tectonic, which doesn't expose it at all).
`/home/arthur/texlive/2026/texmf-dist/web2c/texmf.cnf` currently has:

```
main_memory     = 12000000    % pdftex's real compiled ceiling (see below)
extra_mem_top   = 100000000
extra_mem_bot   = 100000000
pool_size       = 50000000
string_vacancies = 500000
max_strings     = 2000000
```

...followed by `fmtutil-sys --byfmt pdflatex/latex/xelatex` to rebuild the
format files against the new limits — required after any edit here, or
the change has no effect. Two different pools turned out to matter, found
by hitting each one in turn on real documents rather than guessing:

- **`main_memory`**: `pdftex` genuinely cannot go above ~12.5M for this
  build — found by binary search; anything higher fails to even *build*
  the format, with `Ouch---my internal constants have been
  clobbered!---case 14` (a compiled-in bound, not a runtime one).
  `extra_mem_top`/`extra_mem_bot` are separate pools without that ceiling
  — tested working up to 300M each; settled on 100M as comfortable
  headroom without being gratuitous. `xetex` never hit this bound at all
  (tested to 64M on `main_memory` directly, no complaint).
- **`pool_size`/`max_strings`/`string_vacancies`**: hit by
  `Paper/main.tex`, not this document — see `Paper/tools/README.md`.
  Rendering real images as true-vector pixel grids (one `\fill` per pixel)
  creates one dynamically-computed xcolor color string per pixel; at
  262,144 pixels across four images in one document, that exhausts the
  *string pool* (identifiers/color names), a completely different
  resource from `main_memory` (raw token/node memory). The error looks
  the same at a glance (`TeX capacity exceeded`) but names a different
  pool (`pool size=...` vs `main memory size=...`) — worth reading the
  exact error before assuming it's the same knob as last time.

If you reinstall TeX Live from scratch, you'll need to reapply all of
this and rerun `fmtutil-sys` before either figure-heavy document will
compile.

## What's included

21 plots: 9 for grayscale, 9 for RGB888, 3 for RGB565 (RGB565 only has the
sweep pass — no key-sensitivity/histogram/PSNR analysis exists for it).

| Metric | Grayscale | RGB888 | RGB565 |
|---|---|---|---|
| Entropy | `gray_entropy` | `rgb888_entropy` | `rgb565_entropy` |
| Horizontal correlation | `gray_correlation` | `rgb888_correlation` | `rgb565_correlation` |
| NPCR/UACI (plaintext bit flip) | `gray_npcr_uaci_plaintext` | `rgb888_npcr_uaci_plaintext` | `rgb565_npcr_uaci_plaintext` |
| NPCR/UACI (key bit flip) | `gray_npcr_uaci_key` | `rgb888_npcr_uaci_key` | — |
| Wrong-key decryption PSNR | `gray_wrongkey_decryption_psnr` | `rgb888_wrongkey_decryption_psnr` | — |
| NPCR by flipped key-bit position | `gray_bitstudy_npcr` | `rgb888_bitstudy_npcr` | — |
| Cipher chi-square | `gray_chi_square` | `rgb888_chi_square` | — |
| Plain vs cipher PSNR | `gray_psnr_plain_cipher` | `rgb888_psnr_plain_cipher` | — |
| Plain vs wrong-key decryption PSNR | `gray_psnr_plain_wrongkey` | `rgb888_psnr_plain_wrongkey` | — |

Each "NPCR/UACI" plot draws both metrics as two lines in one panel (mean
over images per K), matching the merged panels in the native PDFs.

## What's *not* included, and why

The native `sweep_k*.pdf` reports (grayscale/RGB888) have two more panel
types per project that are **not** reproduced here:

- **Cipher pixel histogram (R/G/B)** — a 256-level-per-channel histogram of
  one representative image's ciphertext.
- **Plain vs cipher chi-square distribution** — a log10(chi-square) bar
  histogram comparing the plaintext and ciphertext chi-square statistics
  across all (image, K) results.

Both are computed ad hoc inside `src/evaluation.cpp` and rendered directly
into the PDF; the underlying counts are never written to any CSV, so there
was no data file to build a TikZ version from. Reproducing them would need
a small C++ change to export that histogram/distribution data (e.g. an
extra CSV per project), which wasn't part of this pass — flag if you want
that added.

## Regenerating

These files were generated from the CSVs by a one-off script, not committed
separately. If the underlying `sweep_k*.csv` files change (e.g. after a
fresh `./run_all_tests.sh` run), regenerate `Tikz/` to match — ask and it'll
be redone from the current CSVs.
