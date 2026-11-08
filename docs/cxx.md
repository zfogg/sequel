# Sequel C++ API

The C++ facade is available from `<sequel/sequel.hpp>` and the CMake target
`sequel::cxx`. It wraps the C engine with C++27 ownership and
`std::expected<T, sequel::error>` results.

```cpp
#include <sequel/sequel.hpp>

auto result = sequel::analyze(
    "SELECT * FROM users WHERE id = $1",
    {.dialect_value = sequel::dialect::postgresql, .format = true});

if (result) {
    std::cout << result->text();
} else {
    std::cerr << result.error().message();
}
```

`sequel::analysis` and `sequel::metadata` own their native storage and are
move-only. Views returned by `text()`, `summary()`, `formatted()`, and related
accessors remain valid while the owning result remains alive. C++ callers do
not free native Sequel results manually.

The C++ facade currently covers inspection, unified analysis, sanitization,
formatting, fingerprints, diagnostics, source maps, bind descriptors, limits,
and cancellation options. The C API remains available for C and ABI-oriented
consumers.
