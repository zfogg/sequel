"""Invalidate cgo's cache when the included C implementation changes."""

import argparse
import hashlib
from pathlib import Path

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--check", action="store_true")
args = parser.parse_args()
digest = hashlib.sha256()
sources = sorted((root / "src/sql").glob("*.c")) + [
    root / "src/utf8.c",
    root / "src/utf8.h",
    root / "include/sequel.h",
]
sources += sorted(p for p in (root / "third_party").rglob("*") if p.suffix in (".c", ".h"))
for path in sources:
    digest.update(path.relative_to(root).as_posix().encode() + b"\0" + path.read_bytes().replace(b"\r\n", b"\n"))
data = (
    f"/* C engine SHA256: {digest.hexdigest()} */\n"
    '#include "../src/sql/main.c"\n#include "../src/utf8.c"\n'
    '#include "../third_party/utf8proc.c"\n'
)
target = root / "go/native.c"
if target.read_text(encoding="utf-8") != data:
    if args.check:
        raise SystemExit("Run uv run python scripts/sync_native.py after editing the C engine")
    target.write_text(data, encoding="utf-8", newline="\n")
