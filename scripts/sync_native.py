"""Invalidate cgo's cache when the included C implementation changes."""

import argparse
import hashlib
from pathlib import Path

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--check", action="store_true")
args = parser.parse_args()
digest = hashlib.sha256()
sources = sorted(p for p in (root / "lib").rglob("*") if p.suffix in (".c", ".h")) + [
    root / "include/sequel.h",
]
sources += sorted(p for p in (root / "third_party").rglob("*") if p.suffix in (".c", ".h"))
for path in sources:
    digest.update(path.relative_to(root).as_posix().encode() + b"\0" + path.read_bytes().replace(b"\r\n", b"\n"))
target = root / "go/native.c"
generated = root / "go"
if not args.check:
    for path in generated.glob("native_lib_*.c"):
        path.unlink()
library_sources = [
    path
    for path in sorted(root.glob("lib/**/*.c"))
    if path.relative_to(root).as_posix() not in {"lib/engine.c", "lib/util/utf8.c", "lib/analysis/token_rules.c"}
]
if not args.check:
    for path in library_sources:
        name = "native_lib_" + "_".join(path.relative_to(root).with_suffix("").parts) + ".c"
        (generated / name).write_text(
            f'#include "../{path.relative_to(root).as_posix()}"\n', encoding="utf-8", newline="\n"
        )
data = (
    f"/* C engine SHA256: {digest.hexdigest()} */\n"
    '#include "../lib/engine.c"\n'
    '#include "../lib/util/utf8.c"\n'
    '#include "../third_party/utf8proc/utf8proc.c"\n'
)
if target.read_text(encoding="utf-8") != data:
    if args.check:
        raise SystemExit("Run uv run python scripts/sync_native.py after editing the C engine")
    target.write_text(data, encoding="utf-8", newline="\n")
