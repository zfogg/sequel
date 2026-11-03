---
name: sequel-release
description: Release Sequel's six-platform CLI and C SDKs, Python wheels/sdist, Go sources and Rust bindings on GitHub, and update consumers when a rollout is requested.
---

# Release Sequel

Read [the release workflow](../../../.github/workflows/release.yml),
[the packager](../../../scripts/package_release.py), and
[release usage](../../../docs/releases.md) when changing versions or assets.
The shared version lives in root `CMakeLists.txt`, `pyproject.toml`, and
`Cargo.toml`. Refresh `uv.lock` with uv and `Cargo.lock` with Cargo after changing
it. Go uses the root `vX.Y.Z` tag; there is no separate `go/vX.Y.Z` module tag.

Before choosing a version, inspect the branch, working tree, existing tags and
GitHub Releases. Commit and push the release candidate. Run relevant local C,
Go, Python and Rust checks, including `uv run python scripts/style.py`,
`go test ./...`, `go vet ./...`, `cargo test --locked`, and
`cargo clippy --locked --all-targets -- -D warnings`.

For a release, dispatch `gh workflow run release.yml --ref BRANCH -f tag=vX.Y.Z`.
Manual dispatch validates without publishing. If API authentication is unavailable
but Git push works, push the candidate to `codex/release-preflight` instead.
The workflow checks all six native OS/architecture pairs: Linux, macOS and
Windows, each on x64 and ARM64. Require every release job to pass before creating
an annotated `vX.Y.Z` tag at that exact tested commit. Push the tag to publish.
If the tested commit changes, validate the changed candidate before tagging.

The native jobs build static and shared C SDK ZIPs, with the CLI in each static
ZIP. Linux CLI binaries have no dynamic interpreter; Windows CLI binaries use
the static CRT. macOS statically links the engine but retains Apple's required
system library. Do not describe it as a fully static macOS executable. Packages
include headers, CMake exports, licenses and a target/file-hash manifest. Native
binary headers determine the architecture, not the packaging Python's host.

Each native job runs C tests with assertions enabled, tests relocated CMake
consumers and Rust static/shared linking, and installs/tests a CPython 3.13 wheel.
The source job tests Go, Python and Rust, packages the Rust `.crate` with its C
sources, builds the Python sdist, and archives complete binding sources. Go and
Rust normally compile their bundled engine; neither invokes the CLI. The Rust
prebuilt-library option requires a matching engine version and target; Windows
static Rust consumers must enable `crt-static`. Keep these contracts in the
README rather than implying that downloading the CLI installs language modules.

After the tag run finishes, verify all twelve native ZIPs, six platform wheels,
the sdist, Rust crate, binding-source ZIP and `SHA256SUMS`. Download/check hashes,
inspect representative contents and confirm the latest release resolves correctly.
GitHub publication does not publish to PyPI or crates.io. Registry publication
and deployment of running consumers are separate tasks.

Version tags are immutable, especially because Go and Cargo resolve source by
tag. Retry a transient runner/network failure without changing its commit. Fix
persistent failures before retrying. If code must change after tagging, publish
a new patch version rather than moving the tag or replacing valid old assets.

# Update consumers

When a rollout is requested, discover current consumers under `~/src/` (Windows)
or `~/src/github.com/zfogg/` (macOS/Linux). Gotel consumes Sequel in CMake, its Go
module, its Python direct Git dependency, and its Rust manifest; update those
pins together where needed and refresh Go/uv/Cargo locks. Spaniel, Ratio and
Stonkpile also have direct Go requirements; Beanaries has a direct Python pin.
ascii-chat usually receives Sequel transitively through its Gotel submodule.
Inspect actual manifests rather than assuming that this list is exhaustive.

Preserve unrelated changes and test the affected consumers against the final
published revision without local replacement directives. Commit/push consumer
changes when requested. Do not restart services, run trading/scraping jobs, or
change live databases merely to update source dependencies.
