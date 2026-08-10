#!/usr/bin/env bash
# Build (with the project's micromamba toolchain) and run the full test/
# evaluation pass for all three projects: grayscale, RGB888, RGB565.
#
# Usage:
#   ./run_all_tests.sh [THREADS]
#
# THREADS defaults to 20. Pass 0 for hardware concurrency.
#
# Each project ends up with ONE combined CSV and ONE combined PDF in its
# results/ directory:
#   xormap_image_matlab_cpp/results/sweep_k_gray_all.csv   + .pdf
#   xormap_image_rgb888_matlab_cpp/results/sweep_k_rgb888.csv + .pdf
#   xormap_image_rgb565_matlab_cpp/results/sweep_k_rgb565_all.csv + .pdf

set -euo pipefail

THREADS="${1:-20}"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GRAY_DIR="$ROOT_DIR/xormap_image_matlab_cpp"
RGB888_DIR="$ROOT_DIR/xormap_image_rgb888_matlab_cpp"
RGB565_DIR="$ROOT_DIR/xormap_image_rgb565_matlab_cpp"

export MAMBA_ROOT_PREFIX="${MAMBA_ROOT_PREFIX:-$HOME/.local/share/micromamba}"
MAMBA="${MAMBA_BIN:-$HOME/.local/bin/micromamba}"
CMAKE=("$MAMBA" run -n xormap cmake)
CTEST=("$MAMBA" run -n xormap ctest)

build() {
  local dir="$1"
  echo "== building $dir =="
  "${CMAKE[@]}" -S "$dir" -B "$dir/build-server" -DCMAKE_BUILD_TYPE=Release
  "${CMAKE[@]}" --build "$dir/build-server" --parallel
}

test_project() {
  local dir="$1"
  echo "== testing $dir =="
  "${CTEST[@]}" --test-dir "$dir/build-server" --output-on-failure --parallel "$THREADS"
}

build "$GRAY_DIR"
build "$RGB888_DIR"
build "$RGB565_DIR"

test_project "$GRAY_DIR"
test_project "$RGB888_DIR"
test_project "$RGB565_DIR"

echo "== grayscale: verify + run-tests =="
"$GRAY_DIR/build-server/xormap_gray" verify
"$GRAY_DIR/build-server/xormap_gray" run-tests --threads "$THREADS"

echo "== RGB888: verify + run-tests =="
"$RGB888_DIR/build-server/xormap_rgb888" verify
"$RGB888_DIR/build-server/xormap_rgb888" run-tests --threads "$THREADS"

echo "== RGB565: verify + convert + sweep-all =="
"$RGB565_DIR/build-server/xormap_rgb565" verify
"$RGB565_DIR/build-server/xormap_rgb565" convert --threads "$THREADS"
"$RGB565_DIR/build-server/xormap_rgb565" sweep-all --threads "$THREADS"

echo
echo "Done. Results:"
echo "  $GRAY_DIR/results/sweep_k_gray_all.csv"
echo "  $GRAY_DIR/results/sweep_k_gray_all.pdf"
echo "  $RGB888_DIR/results/sweep_k_rgb888.csv"
echo "  $RGB888_DIR/results/sweep_k_rgb888.pdf"
echo "  $RGB565_DIR/results/sweep_k_rgb565_all.csv"
echo "  $RGB565_DIR/results/sweep_k_rgb565_all.pdf"
