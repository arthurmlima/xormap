# Figure data export tools

Two throwaway C++ programs that encrypt one real image with the project's
actual cipher (same library code as everything else — no reimplementation)
and export what `Paper/main.tex`'s vector figures need: plain/cipher TIFFs,
a 256-level pixel histogram (per channel for RGB888), and horizontal
adjacent-pixel correlation sample pairs (`collect_pairs=true`).

## Build

```sh
export MAMBA_ROOT_PREFIX=/home/arthur/.local/share/micromamba
ENV=/home/arthur/.local/share/micromamba/envs/xormap
GRAY=/home/arthur/xormap/xormap_image_matlab_cpp
RGB888=/home/arthur/xormap/xormap_image_rgb888_matlab_cpp
CXX="$ENV/bin/x86_64-conda-linux-gnu-c++"

micromamba run -n xormap "$CXX" -std=c++17 -O2 \
  -I"$GRAY/include" \
  export_gray_figure.cpp "$GRAY/build-server/libxormap_image.a" \
  -L"$ENV/lib" -ltiff -lcrypto -lpthread -Wl,-rpath,"$ENV/lib" \
  -o export_gray_figure

micromamba run -n xormap "$CXX" -std=c++17 -O2 \
  -I"$GRAY/include" -I"$RGB888/include" \
  export_rgb888_figure.cpp \
  "$RGB888/build-server/libxormap_color_core.a" \
  "$RGB888/build-server/libxormap_rgb888.a" \
  "$GRAY/build-server/libxormap_image.a" \
  -L"$ENV/lib" -ltiff -lcrypto -lpthread -lcairo -Wl,-rpath,"$ENV/lib" \
  -o export_rgb888_figure
```

(`-lcairo` is needed for the RGB888 tool only because `common.cpp` bundles
a PDF-report writer in the same translation unit as the functions actually
used; nothing in this tool calls it.)

## Run

```sh
./export_gray_figure   "$GRAY/images/5.3.01.tiff" 384 ../data gray
./export_rgb888_figure "$RGB888/images/house.tiff" 384 ../data house
```

Then downsample the plain/cipher TIFFs to the small PNGs `main.tex`
actually embeds (the correlation/histogram CSVs are used at full
resolution directly — no thinning needed once TeX Live's engine memory is
raised per `Tikz/README.md`):

```python
from PIL import Image
for name in ("gray_plain", "gray_cipher", "house_plain", "house_cipher"):
    im = Image.open(f"../data/{name}.tiff")
    im.thumbnail((300, 300), Image.LANCZOS)
    im.save(f"../img/{name}.png", optimize=True)
```
