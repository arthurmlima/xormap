# RGB888 MATLAB-to-C++ map

| MATLAB source | Native C++ implementation |
|---|---|
| `rgb888_pack.m`, `rgb888_unpack.m` | `xormap_color::pack_rgb888`, `unpack_rgb888` |
| `words24_to_bytes_be.m` | `xormap_color::words_to_bytes_be(..., 24)` |
| `xormap_keystream_words_fast.m` | `xormap_color::keystream_words_fast` |
| `xormap_rgb888_encrypt.m`, `xormap_rgb888_decrypt.m` | generic packed-word encrypt/decrypt in `common.cpp` |
| `test_xormap_rgb888.m` | Catch2 common/RGB888 tests plus `xormap_rgb888 verify` |
| `sweep_k_rgb888.m` | `xormap_color::rgb888::sweep` and CLI `sweep` |
| `analysis_rgb888.m` | `xormap_color::rgb888::analysis` and CLI `analysis` |
| `parfor` task grid | exception-safe native `std::thread` dynamic scheduler |
| MATLAB worker `rng` | exact `MatlabThreefry` implementation |
| MATLAB client `rng(...,'twister')` | exact `MatlabTwister` implementation |

The C++ tests include the real `4.1.01`, K=24 sweep and analysis vectors.
Those values match the checked-in MATLAB CSV rows exactly at every emitted
decimal place.
