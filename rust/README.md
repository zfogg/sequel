# Sequel for Rust

Safe Rust bindings to the same C engine used by the CLI, Go, and Python.
SQL is never executed. Formatting/highlighting preserve literals; unified
analysis and default sanitization redact them.

```toml
[dependencies]
sequel = { git = "https://github.com/zfogg/sequel", tag = "v0.2.0" }
```

```rust
use sequel::{analyze, AnalysisOptions, Dialect};

let options = AnalysisOptions { dialect: Dialect::PostgreSql, format: true, ..Default::default() };
let result = analyze("SELECT secret FROM widgets WHERE id = $1", &options)?;
assert_eq!(result.binds.len(), 1);
result.check_binds(0, 1)?;
# Ok::<(), sequel::Error>(())
```

Cargo compiles the bundled C sources using a C11 compiler; there is no dependency
on an installed `sequel` executable, CMake, libclang, or a runtime download.
The `.crate` release asset includes the engine and third-party licenses. Extract
it and use a Cargo `path` dependency if you prefer release downloads to Git.

To use a matching release's prebuilt C libraries, extract its archive, set
`SEQUEL_LIB_DIR` to its `lib` directory, and build with Cargo. Static linking is
the default and requires both `sequel` and `sequel_utf8proc` archives. Set
`SEQUEL_LINK_KIND=dylib` to use its shared library instead, and make that library
available to the OS loader (`PATH` on Windows, loader search paths on Unix).
Use the same Sequel version and target architecture for crate and library.

`analyze_bytes` diagnoses invalid UTF-8 and embedded NULs without truncation.
Analysis failures return an owning result with status, omission reason, and
canonical diagnostics; no partial SQL is exported. All native storage is freed
before returning Rust-owned data. Budgets measure native work/memory; Rust's
copied result allocations are additional. Cancellation callbacks are synchronous;
panics are caught before crossing C and become an error. Source maps operate on
UTF-8 byte ranges; UTF-16 conversion helpers reject split scalars/surrogates.

Run `cargo test --locked --features abi-check` (also compiles a C layout probe),
`cargo clippy --all-targets --all-features -- -D warnings`, and
`cargo doc --no-deps`. Full API documentation is available through `cargo doc`.
