"""Build relocatable, architecture-checked Sequel release archives."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import struct
import subprocess
import tomllib
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TAG = re.compile(r"v[0-9]+\.[0-9]+\.[0-9]+\Z")


def validate_version(tag: str) -> str:
    if not TAG.fullmatch(tag):
        raise ValueError("Expected vMAJOR.MINOR.PATCH")
    version = tag[1:]
    for manifest in ("pyproject.toml", "Cargo.toml"):
        data = tomllib.loads((ROOT / manifest).read_text(encoding="utf-8"))
        actual = data["project" if manifest == "pyproject.toml" else "package"]["version"]
        if actual != version:
            raise ValueError(f"{manifest} version {actual} does not match {tag}")
    if f"project(sequel VERSION {version} LANGUAGES C)" not in (ROOT / "CMakeLists.txt").read_text():
        raise ValueError("CMake version does not match the release")
    return version


def revision() -> str:
    return subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()


def binary_target(data: bytes) -> tuple[str, str]:
    """Read the target from ELF, Mach-O or PE; never infer it from the Python host."""
    if data[:4] == b"\x7fELF" and len(data) >= 64 and data[4:6] == b"\x02\x01":
        machine = struct.unpack_from("<H", data, 18)[0]
        return "linux", {62: "x64", 183: "arm64"}[machine]
    if data[:4] == b"\xcf\xfa\xed\xfe" and len(data) >= 32:
        machine = struct.unpack_from("<I", data, 4)[0]
        return "macos", {0x01000007: "x64", 0x0100000C: "arm64"}[machine]
    if data[:2] == b"MZ" and len(data) >= 64:
        offset = struct.unpack_from("<I", data, 0x3C)[0]
        if data[offset : offset + 4] == b"PE\0\0":
            return "windows", {0x8664: "x64", 0xAA64: "arm64"}[struct.unpack_from("<H", data, offset + 4)[0]]
    raise ValueError("Not a supported 64-bit release binary")


def check_binary(path: Path, system: str, arch: str, *, static_cli: bool = False) -> None:
    data = path.read_bytes()
    if binary_target(data) != (system, arch):
        raise ValueError(f"Wrong target architecture in {path.name}")
    if system == "linux" and static_cli:
        phoff = struct.unpack_from("<Q", data, 32)[0]
        entry_size, count = struct.unpack_from("<HH", data, 54)
        if entry_size < 4 or phoff + entry_size * count > len(data):
            raise ValueError("Invalid ELF program header table")
        if any(struct.unpack_from("<I", data, phoff + i * entry_size)[0] == 3 for i in range(count)):
            raise ValueError("Linux static CLI has a dynamic interpreter")


def package_native(tag: str, stage: Path, system: str, arch: str, linkage: str, output: Path) -> Path:
    validate_version(tag)
    stage = stage.resolve()
    for path in (stage / "include/sequel.h", stage / "lib/cmake/sequel/sequelConfig.cmake"):
        if not path.is_file():
            raise FileNotFoundError(path)
    if linkage == "static":
        suffix, prefix = (".lib", "") if system == "windows" else (".a", "lib")
        for library in ("sequel", "sequel_utf8proc"):
            if not (stage / "lib" / f"{prefix}{library}{suffix}").is_file():
                raise FileNotFoundError(f"Missing static {library} archive")
        check_binary(stage / "bin" / ("sequel.exe" if system == "windows" else "sequel"), system, arch, static_cli=True)
    else:
        candidates = list(
            (stage / ("bin" if system == "windows" else "lib")).glob(
                "sequel.dll" if system == "windows" else "libsequel*.dylib" if system == "macos" else "libsequel.so*"
            )
        )
        if not candidates:
            raise FileNotFoundError("Missing shared library")
        for library in candidates:
            check_binary(library, system, arch)
    name = f"sequel-{tag}-{system}-{arch}-{linkage}"
    output.mkdir(parents=True, exist_ok=True)
    destination = output / f"{name}.zip"
    files = sorted(p for p in stage.rglob("*") if p.is_file())
    manifest = {
        "version": tag[1:],
        "revision": revision(),
        "os": system,
        "arch": arch,
        "linkage": linkage,
        "cli": linkage == "static",
        "windows_crt": "static" if system == "windows" and linkage == "static" else None,
        "files": {p.relative_to(stage).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in files},
    }
    with zipfile.ZipFile(destination, "w", zipfile.ZIP_DEFLATED) as archive:
        for path in files:
            archive.write(path, f"{name}/{path.relative_to(stage).as_posix()}")
        archive.write(ROOT / "docs/releases.md", f"{name}/README.md")
        archive.writestr(f"{name}/manifest.json", json.dumps(manifest, indent=2) + "\n")
    return destination


def package_bindings(tag: str, output: Path) -> Path:
    validate_version(tag)
    name = f"sequel-bindings-{tag}"
    files = subprocess.check_output(["git", "ls-files", "-z"], cwd=ROOT).decode().split("\0")
    output.mkdir(parents=True, exist_ok=True)
    destination = output / f"{name}.zip"
    with zipfile.ZipFile(destination, "w", zipfile.ZIP_DEFLATED) as archive:
        for filename in files:
            if filename and (ROOT / filename).is_file():
                archive.write(ROOT / filename, f"{name}/{filename}")
    return destination


def assemble_release(tag: str, stage: Path, output: Path) -> None:
    """Flatten Actions' preserved artifact directories, rejecting missing/conflicting files."""
    validate_version(tag)
    expected = {
        f"sequel-{tag}-{system}-{arch}-{linkage}.zip"
        for system in ("linux", "macos", "windows")
        for arch in ("x64", "arm64")
        for linkage in ("static", "shared")
    }
    expected.update((f"sequel-{tag[1:]}.crate", f"sequel-{tag[1:]}.tar.gz", f"sequel-bindings-{tag}.zip"))
    sources = {}
    for path in sorted(stage.rglob("*")):
        if not path.is_file() or path.name == "SHA256SUMS":
            continue
        if not path.name.endswith((".zip", ".whl", ".crate", ".tar.gz")):
            raise ValueError(f"Unexpected release artifact: {path.name}")
        previous = sources.get(path.name)
        if previous and previous.read_bytes() != path.read_bytes():
            if (
                path.name.endswith("_universal2.whl")
                and universal_macos_wheel(previous)
                and universal_macos_wheel(path)
            ):
                # Both macOS runners build the same two-architecture wheel. Each
                # tests its native slice; retain the first validated universal
                # payload deterministically instead of overwriting raced bytes.
                continue
            raise ValueError(f"Conflicting release artifact: {path.name}")
        sources[path.name] = path
    wheel_count = sum(name.endswith(".whl") for name in sources)
    universal = any(name.endswith("_universal2.whl") for name in sources)
    if (
        not expected.issubset(sources)
        or wheel_count != (5 if universal else 6)
        or len(sources) != len(expected) + wheel_count
    ):
        raise ValueError(f"Incomplete release artifacts: {sorted(sources)}")
    output.mkdir(parents=True, exist_ok=True)
    for name, source in sources.items():
        shutil.copy2(source, output / name)


def universal_macos_wheel(path: Path) -> bool:
    """Require a genuine x64+ARM64 Mach-O extension before deduplicating wheels."""
    with zipfile.ZipFile(path) as archive:
        extensions = [name for name in archive.namelist() if name.endswith(".so")]
        if len(extensions) != 1:
            return False
        data = archive.read(extensions[0])
    if len(data) < 48 or data[:8] != b"\xca\xfe\xba\xbe\x00\x00\x00\x02":
        return False
    targets = set()
    for index in range(2):
        cpu, _, offset, size, _ = struct.unpack_from(">IIIII", data, 8 + index * 20)
        if offset + size > len(data) or size < 32:
            return False
        target = binary_target(data[offset : offset + size])
        if cpu not in (0x01000007, 0x0100000C):
            return False
        targets.add(target)
    return targets == {("macos", "x64"), ("macos", "arm64")}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kind", choices=["native", "bindings", "validate", "assemble", "checksums"])
    parser.add_argument("--tag", required=True)
    parser.add_argument("--stage", type=Path)
    parser.add_argument("--os", choices=["linux", "macos", "windows"])
    parser.add_argument("--arch", choices=["x64", "arm64"])
    parser.add_argument("--linkage", choices=["static", "shared"])
    parser.add_argument("--output", type=Path, default=ROOT / "dist/release")
    args = parser.parse_args()
    validate_version(args.tag)
    if args.kind == "validate":
        return
    if args.kind == "assemble":
        if not args.stage:
            parser.error("assemble needs --stage")
        assemble_release(args.tag, args.stage, args.output)
    elif args.kind == "native":
        if not all([args.stage, args.os, args.arch, args.linkage]):
            parser.error("native packaging needs --stage, --os, --arch and --linkage")
        print(package_native(args.tag, args.stage, args.os, args.arch, args.linkage, args.output))
    elif args.kind == "bindings":
        print(package_bindings(args.tag, args.output))
    else:
        files = sorted(p for p in args.output.iterdir() if p.is_file() and p.name != "SHA256SUMS")
        (args.output / "SHA256SUMS").write_text(
            "".join(f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n" for p in files), encoding="utf-8"
        )


if __name__ == "__main__":
    main()
