"""Verify actual release target metadata, layout and missing-artifact failures."""

import importlib.util
import struct
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("package_release", ROOT / "scripts/package_release.py")
assert spec and spec.loader
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)


def binary(system, arch, interpreter=False):
    data = bytearray(160)
    if system == "linux":
        data[:6] = b"\x7fELF\x02\x01"
        struct.pack_into("<H", data, 18, 62 if arch == "x64" else 183)
        struct.pack_into("<Q", data, 32, 64)
        struct.pack_into("<HH", data, 54, 56, 1)
        struct.pack_into("<I", data, 64, 3 if interpreter else 1)
    elif system == "macos":
        data[:4] = b"\xcf\xfa\xed\xfe"
        struct.pack_into("<I", data, 4, 0x01000007 if arch == "x64" else 0x0100000C)
    else:
        data[:2] = b"MZ"
        struct.pack_into("<I", data, 0x3C, 64)
        data[64:68] = b"PE\0\0"
        struct.pack_into("<H", data, 68, 0x8664 if arch == "x64" else 0xAA64)
    return data


class ReleaseTests(unittest.TestCase):
    def test_all_six_architectures_and_static_interpreter(self):
        with tempfile.TemporaryDirectory() as temporary:
            executable = Path(temporary) / "binary"
            for system in ("linux", "macos", "windows"):
                for arch in ("x64", "arm64"):
                    with self.subTest(system=system, arch=arch):
                        executable.write_bytes(binary(system, arch))
                        release.check_binary(executable, system, arch, static_cli=True)
                        with self.assertRaisesRegex(ValueError, "Wrong target"):
                            release.check_binary(executable, system, "arm64" if arch == "x64" else "x64")
            executable.write_bytes(binary("linux", "x64", interpreter=True))
            with self.assertRaisesRegex(ValueError, "dynamic interpreter"):
                release.check_binary(executable, "linux", "x64", static_cli=True)

    def test_native_payload_is_relocatable_and_complete(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            stage = root / "stage"
            files = {
                "include/sequel.h": b"header",
                "lib/cmake/sequel/sequelConfig.cmake": b"config",
                "lib/libsequel.a": b"archive",
                "lib/libsequel_utf8proc.a": b"archive",
                "bin/sequel": binary("linux", "arm64"),
            }
            for path, data in files.items():
                target = stage / path
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
            with patch.object(release, "revision", return_value="0" * 40):
                result = release.package_native("v0.2.0", stage, "linux", "arm64", "static", root)
            with zipfile.ZipFile(result) as archive:
                prefix = "sequel-v0.2.0-linux-arm64-static/"
                for path in files:
                    self.assertIn(prefix + path, archive.namelist())
                self.assertIn(prefix + "README.md", archive.namelist())
                self.assertIn(prefix + "manifest.json", archive.namelist())
            (stage / "lib/libsequel_utf8proc.a").unlink()
            with self.assertRaises(FileNotFoundError):
                release.package_native("v0.2.0", stage, "linux", "arm64", "static", root)

    def test_version_and_unsupported_inputs(self):
        for tag in ("main", "../v0.2.0", "v1.2", "v9.8.7"):
            with self.assertRaises(ValueError):
                release.validate_version(tag)
        with self.assertRaises(ValueError):
            release.binary_target(b"not a native binary")


if __name__ == "__main__":
    unittest.main()
