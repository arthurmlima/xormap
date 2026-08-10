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
  data/*.csv        the data each plot reads, three files per plot:
                     <name>_points.csv        — every individual (image, K) value (full data)
                     <name>_points_sample.csv — evenly-sampled subset, ~200-250 points,
                                                 used only for the rendered scatter (see below)
                     <name>_means.csv         — mean over images at each K
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

Or compile `Tikz/main.tex` directly to get every plot in one PDF. This
environment had no LaTeX toolchain installed and no sudo access, so it was
verified with [tectonic](https://tectonic-typesetting.github.io/) (a
self-contained TeX engine with no separate texlive-core install), pulled in
via a local, non-root micromamba environment:

```sh
micromamba create -n latex -c conda-forge tectonic
cd Tikz && micromamba run -n latex tectonic main.tex
```

This produces a 21-figure, 10-page PDF. It should compile the same way with
a regular `pdflatex`/`xelatex`/`lualatex` install as long as `pgfplots` is
available (`pdflatex -output-directory Tikz Tikz/main.tex`, run from the
repo root, or `cd Tikz && pdflatex main.tex`).

Each plot file references its data as `data/<file>.csv` — a path relative
to wherever compilation runs from (`Tikz/`, i.e. the directory containing
`main.tex`), **not** relative to the plot file's own location in `plots/`
— that's just how LaTeX resolves `\input`. Compile from inside `Tikz/`, or
from the repo root, keeping `config.tex`, `plots/`, and `data/` together.

### Why the scatter series is a sampled subset, not the full data

The first full-document compile hit tectonic's fixed TeX memory limit
(`main memory size=5000000`) partway through — plotting the complete
per-image scatter (2544 points for grayscale, repeated across many large
figures in one document) accumulates too much memory across a 21-figure
run. The `_points.csv` files still hold every value; only the rendered
scatter marks use `_points_sample.csv`, an evenly-strided subset capped at
~200-250 points per plot, enough to show the spread without exhausting
memory. The mean line and ideal reference line — the two things doing most
of the actual work in each plot — always use the full data.

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
