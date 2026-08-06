# C++ XOR-map model

The model is one function. It accepts a `K`-bit number and returns the `K`-bit
result produced by `Genv_xormap.m`:

```cpp
#include "xormap.hpp"

std::size_t K = 32;
xormap::Integer input = 0x12345678;
xormap::Integer result = xormap::transform(input, K);
```

`xormap::Integer` is `boost::multiprecision::cpp_int`, so the function works for
every `K > 4`, including runtime-selected widths above 64 bits. The Makefile
uses the Homebrew Boost headers in `/opt/homebrew/include`.

Run the comparison tests with:

```sh
make test
```
