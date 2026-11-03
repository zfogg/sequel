# Release downloads and bindings

Download assets from [GitHub Releases](https://github.com/zfogg/sequel/releases).
Verify their hashes against `SHA256SUMS` before using them. Native ZIP names are
`sequel-vVERSION-OS-ARCH-static.zip` or `...-shared.zip`, for Linux, macOS and
Windows, each on x64 and ARM64. `manifest.json` records the commit, target and
individual installed file hashes. Extract an archive with its directory layout.

The static ZIP includes `bin/sequel` (`sequel.exe` on Windows), static libraries,
headers, licenses and relocatable CMake package files. The CLI statically links
Sequel and utf8proc. Linux also links libc statically; Windows uses the static
MSVC CRT. macOS retains Apple's required system `libSystem` dependency: a fully
static macOS executable is not supported. The shared ZIP supplies the shared
library, headers and CMake package; it does not supply another CLI.

The executable and language bindings are independent ways to use the engine.
No binding invokes or requires the installed executable.

## C / C++

Extract a matching native ZIP. Configure your consumer with
`-DCMAKE_PREFIX_PATH=/path/to/extracted/archive`, then use:

```cmake
find_package(sequel CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE sequel::sequel)
```

Use `<sequel.h>`. CMake handles the companion static utf8proc archive. For shared
builds, put the archive's `bin` (Windows) or `lib` (Unix) on the runtime loader
path. Windows static consumers must also use the static MSVC CRT (`/MT`).

## Python

Release wheels are compiled for CPython 3.13 on all six platform targets. Install
the wheel matching your interpreter/OS/architecture with `uv pip install
/path/to/sequel-...whl` in your environment. Then `import sequel` as usual.
These wheels already contain the C engine; no separate ZIP or compiler is needed.

For other supported Python versions (3.11+), use the release sdist:
`uv pip install /path/to/sequel-VERSION.tar.gz`. This compiles the engine with a
local C compiler, without an installed native SDK or CLI. macOS release wheels
and native binaries target macOS 14 or newer; Linux wheels use the runner's
glibc ABI, so source installs are the portable option on older distributions or
musl. Wheel platform tags describe these requirements.

## Rust

The `sequel-VERSION.crate` source archive contains the Rust API and C sources.
Extract it and add `sequel = { path = "/path/to/sequel-VERSION" }` to Cargo.toml,
or use `sequel = { git = "https://github.com/zfogg/sequel", tag = "vVERSION" }`.
Cargo builds the bundled C engine by default; a C11 compiler is required.

To link a matching native release instead, set `SEQUEL_LIB_DIR` to its `lib`
directory. Static is the default; `SEQUEL_LINK_KIND=dylib` selects the shared
library. On Windows, prebuilt static libraries additionally require
`RUSTFLAGS=-Ctarget-feature=+crt-static` so Rust and C agree on the CRT. Shared
libraries must be discoverable by the runtime loader. Use matching versions and
architectures, including the corresponding Rust target triple.

See [Rust usage](../rust/README.md) and `cargo doc --open` for the safe API.

## Go and complete binding sources

`go get github.com/zfogg/sequel@vVERSION` uses the root module tag. The package
import remains `github.com/zfogg/sequel/go`. It bundles the C engine and requires
cgo and a C compiler; it does not load a prebuilt C ZIP.

`sequel-bindings-vVERSION.zip` preserves the complete source layout for C, Go,
Python and Rust, including the engine, manifests and licenses. Keep the Go
directory beside `src`, `include` and `third_party`; copying `go/` alone breaks
its C includes. Use a local Go module replacement for an extracted source bundle,
or prefer the immutable module tag. No package is published to PyPI or crates.io
by the GitHub release workflow.

## Build and publish

The Release workflow has six native platform jobs. Each builds/tests static and
shared C libraries, verifies installed CMake consumers, tests bundled and prebuilt
Rust linking, and builds/tests a native Python wheel. A source job validates Go,
packages the Rust crate and Python sdist, and archives binding sources. It checks
versions across CMake, Python and Cargo. Manual dispatch is validation-only;
only immutable `vMAJOR.MINOR.PATCH` tags publish assets and checksums.
