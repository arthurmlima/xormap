#!/usr/bin/env python3
"""Render a TIFF as a true-vector TikZ picture: one \\fill rectangle per
pixel, downsampled (box filter) to n x n so file size and compile time stay
bounded. Compile time/file size scale ~linearly with n^2 (measured on this
machine: 256x256 ~= 50s, ~1.2MB PDF contribution per image) -- this is a
vector-primitive-count constraint, not a TeX memory one, so raising
main_memory does not help here; it's the 2544-point pgfplots scatter case
(see Tikz/README.md) that needed that.

Usage: gen_vector_image.py <image.tiff> <out.tex> <n> <L|RGB> <width_expr>

<width_expr> is a raw TeX dimension expression (e.g. '0.235\\textwidth')
used directly as the picture's total width via x=<width_expr>/n,
y=<width_expr>/n -- sized natively, never scaled after the fact.
"""
import sys
from PIL import Image


def gen(src, out_tex, n, mode, width_expr):
    im = Image.open(src)
    im = im.convert('L' if mode == 'L' else 'RGB')
    im = im.resize((n, n), Image.BOX)
    px = im.load()

    lines = [r'\begin{tikzpicture}[x=%s/%d,y=%s/%d]' % (width_expr, n, width_expr, n)]
    for y in range(n):
        row = []
        for x in range(n):
            v = px[x, y]
            r, g, b = (v, v, v) if mode == 'L' else v
            yy = n - 1 - y
            row.append(r'\fill[fill={rgb,255:red,%d;green,%d;blue,%d}] (%d,%d) rectangle ++(1,1);'
                       % (r, g, b, x, yy))
        lines.append(''.join(row))
    lines.append(r'\end{tikzpicture}')

    with open(out_tex, 'w') as f:
        f.write('\n'.join(lines) + '\n')
    print(f"{out_tex}: {n}x{n} = {n * n} fills")


if __name__ == '__main__':
    gen(sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4], sys.argv[5])
