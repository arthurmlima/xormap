# RGB565 MATLAB-to-C++ map

| MATLAB source | Native C++ implementation |
|---|---|
| `rgb888_to_rgb565.m` | `xormap_color::pack_rgb565` |
| `rgb565_to_channels.m` | `xormap_color::rgb565_channels` |
| `rgb565_to_rgb888_preview.m` | `xormap_color::preview_rgb565` |
| `bits_to_words.m`, `words_to_bytes_be.m` | generic packed-word helpers in `common.cpp` |
| `xormap_keystream_words.m` | canonical oracle plus fast equivalent word stream |
| `xormap_rgb565_encrypt.m`, `xormap_rgb565_decrypt.m` | generic packed-word encrypt/decrypt |
| `run_all_rgb565.m` | `xormap_rgb565::run_all` and CLI `run-all` |
| `sweep_k_rgb565.m` | `xormap_rgb565::sweep_legacy` and CLI `sweep` |
| original three-image downloader | `convert` consumes the full RGB888 manifest |
| requested all-image calculations | `xormap_rgb565::sweep_all` and CLI `sweep-all` |

The new full-corpus workflow retains RGB565's per-channel 5/6/5 ideals and
packed-word 16-bit NPCR/UACI ideal while applying the RGB888 51-image task
grid and native parallel scheduler.
