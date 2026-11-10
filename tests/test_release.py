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
    def test_actions_artifact_layout_is_flattened_and_validated(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            stage, output = root / "downloaded", root / "release"
            paths = []
            for system in ("linux", "macos", "windows"):
                for arch in ("x64", "arm64"):
                    prefix = stage / f"release-{system}-{arch}"
                    for linkage in ("static", "shared"):
                        paths.append(prefix / "release" / f"sequel-v0.2.0-{system}-{arch}-{linkage}.zip")
                    paths.append(prefix / "wheels" / f"sequel-0.2.0-{system}-{arch}.whl")
            paths.extend(
                stage / "sources" / name
                for name in ("sequel-0.2.0.crate", "sequel-0.2.0.tar.gz", "sequel-bindings-v0.2.0.zip")
            )
            for path in paths:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(path.name.encode())
            release.assemble_release("v0.2.0", stage, output)
            self.assertEqual(len(list(output.iterdir())), 21)
            for path in paths:
                self.assertEqual((output / path.name).read_bytes(), path.read_bytes())
            duplicate = stage / "duplicate" / paths[0].name
            duplicate.parent.mkdir()
            duplicate.write_bytes(b"conflicting upload")
            with self.assertRaisesRegex(ValueError, "Conflicting"):
                release.assemble_release("v0.2.0", stage, output)
            duplicate.unlink()
            paths[0].unlink()
            with self.assertRaisesRegex(ValueError, "Incomplete"):
                release.assemble_release("v0.2.0", stage, output)

    def test_universal_wheels_must_contain_both_native_slices(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "sequel_universal2.whl"
            fat = bytearray(384)
            fat[:8] = b"\xca\xfe\xba\xbe\x00\x00\x00\x02"
            for index, arch in enumerate(("x64", "arm64")):
                offset = 64 + index * 160
                struct.pack_into(
                    ">IIIII", fat, 8 + index * 20, 0x01000007 if arch == "x64" else 0x0100000C, 0, offset, 160, 0
                )
                fat[offset : offset + 160] = binary("macos", arch)
            with zipfile.ZipFile(path, "w") as archive:
                archive.writestr("sequel/_sql.so", fat)
            self.assertTrue(release.universal_macos_wheel(path))
            fat[224:384] = binary("macos", "x64")
            with zipfile.ZipFile(path, "w") as archive:
                archive.writestr("sequel/_sql.so", fat)
            self.assertFalse(release.universal_macos_wheel(path))

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
                "include/sequel/status.h": b"header",
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
