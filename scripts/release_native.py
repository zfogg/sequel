"""Build, install, package and consume both native Sequel linkage variants."""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import zipfile

from package_release import ROOT, package_native


def run(*args: str, env: dict[str, str] | None = None, **kwargs) -> None:
    subprocess.run(args, cwd=ROOT, env=env, check=True, **kwargs)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--os", required=True, choices=["linux", "macos", "windows"])
    parser.add_argument("--arch", required=True, choices=["x64", "arm64"])
    parser.add_argument("--target", required=True)
    args = parser.parse_args()
    base = ROOT / "build/release"
    for linkage in ("static", "shared"):
        build, stage = base / linkage, base / f"stage-{linkage}"
        options = [
            "-G",
            "Ninja",
            "-DCMAKE_BUILD_TYPE=Release",
            "-DCMAKE_C_STANDARD=11",
            "-DCMAKE_C_EXTENSIONS=OFF",
            f"-DBUILD_SHARED_LIBS={'ON' if linkage == 'shared' else 'OFF'}",
            "-DSEQUEL_BUILD_TESTS=ON",
            f"-DSEQUEL_BUILD_CLI={'ON' if linkage == 'static' else 'OFF'}",
            f"-DSEQUEL_STATIC_CLI={'ON' if linkage == 'static' else 'OFF'}",
            f"-DPython3_EXECUTABLE={sys.executable}",
        ]
        if args.os == "windows":
            options += ["-DCMAKE_C_COMPILER=clang-cl", f"-DCMAKE_C_COMPILER_TARGET={args.target}"]
        if args.os == "macos":
            options += ["-DCMAKE_OSX_DEPLOYMENT_TARGET=14.0"]
        run("cmake", "-S", ".", "-B", str(build), *options)
        run("cmake", "--build", str(build), "--parallel", "4")
        run("ctest", "--test-dir", str(build), "--output-on-failure", "--parallel", "4")
        run("cmake", "--install", str(build), "--prefix", str(stage))
        archive = package_native(args.tag, stage, args.os, args.arch, linkage, ROOT / "dist/release")
        # Test the relocated download, rather than accidentally linking the build tree.
        extracted = base / f"extracted-{linkage}"
        extracted.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(archive) as package:
            package.extractall(extracted)
        prefix = extracted / archive.stem
        env = os.environ.copy()
        env["PATH"] = str(prefix / "bin") + os.pathsep + env.get("PATH", "")
        loader = "DYLD_LIBRARY_PATH" if args.os == "macos" else "LD_LIBRARY_PATH"
        env[loader] = str(prefix / "lib") + os.pathsep + env.get(loader, "")
        consumer = base / f"consumer-{linkage}"
        consumer_options = ["-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_PREFIX_PATH={prefix}"]
        if args.os == "windows":
            consumer_options += ["-DCMAKE_C_COMPILER=clang-cl", f"-DCMAKE_C_COMPILER_TARGET={args.target}"]
            if linkage == "static":
                consumer_options += ["-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded"]
        run("cmake", "-S", "tests/cmake-consumer", "-B", str(consumer), *consumer_options, env=env)
        run("cmake", "--build", str(consumer), env=env)
        run("ctest", "--test-dir", str(consumer), "--output-on-failure", env=env)
        if linkage == "static":
            executable = prefix / "bin" / ("sequel.exe" if args.os == "windows" else "sequel")
            # ZIP extraction on Unix does not restore executable permission bits.
            executable.chmod(executable.stat().st_mode | 0o111)
            formatted = subprocess.check_output([str(executable), "format", "select '<value>'"], text=True, env=env)
            rendered = subprocess.check_output([str(executable), "highlight"], input=formatted, text=True, env=env)
            if "&lt;value&gt;" not in rendered or "<pre" not in rendered:
                raise RuntimeError("Extracted CLI format/highlight pipeline failed")
        env["SEQUEL_LIB_DIR"] = str(prefix / "lib")
        env["SEQUEL_LINK_KIND"] = "static" if linkage == "static" else "dylib"
        env["CARGO_TARGET_DIR"] = str(base / f"rust-{linkage}")
        if args.os == "windows" and linkage == "static":
            env["RUSTFLAGS"] = env.get("RUSTFLAGS", "") + " -Ctarget-feature=+crt-static"
        run("cargo", "test", "--locked", "--features", "abi-check", "--target", args.target, env=env)


if __name__ == "__main__":
    main()
