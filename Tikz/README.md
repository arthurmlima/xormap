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
                     <name>_points.csv  — every individual (image, K) value
                     <name>_means.csv   — mean over images at each K
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
pdflatex -output-directory Tikz Tikz/main.tex
```

(No LaTeX toolchain was available in this environment to test-compile it —
review the generated `.tex` before a from-scratch build if you hit issues.)

Each plot file references its data as `../data/<file>.csv` — a path relative
to `plots/` — so keep `config.tex`, `plots/`, and `data/` together if you
move `Tikz/` elsewhere.

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
