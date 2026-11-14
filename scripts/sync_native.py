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
library_sources = [
    path
    for path in sorted(root.glob("lib/**/*.c"))
    if path.relative_to(root).as_posix() not in {"lib/engine.c", "lib/util/utf8.c", "lib/analysis/token_rules.c"}
]
native_library = generated / "native_lib.c"
local_symbol_aliases = {
    "lib/summary/body.c": {"summary_ellipsis": "summary_body_ellipsis"},
    "lib/summary/cte.c": {"summary_ellipsis": "summary_cte_ellipsis"},
    "lib/summary/normalize.c": {
        "summary_ellipsis": "summary_normalize_ellipsis",
        "ellipsis": "summary_normalize_ellipsis",
    },
    "lib/summary/recovery.c": {"summary_ellipsis": "summary_recovery_ellipsis"},
    "lib/summary/render/query.c": {"summary_ellipsis": "summary_render_ellipsis"},
    "lib/summary/special.c": {"summary_ellipsis": "summary_special_ellipsis"},
    "lib/summary/statement.c": {"ellipsis": "summary_statement_ellipsis"},
    "lib/summary/subquery.c": {"ellipsis": "summary_subquery_ellipsis"},
}


def native_include(path):
    relative = path.relative_to(root).as_posix()
    aliases = local_symbol_aliases.get(relative, {})
    resource_scope = relative.startswith("lib/analysis/") or relative == "lib/tokens.c"
    directives = []
    if resource_scope:
        directives.extend(
            [
                "#define malloc sequel_alloc\n",
                "#define calloc sequel_calloc\n",
                "#define realloc sequel_realloc\n",
                "#define free sequel_dealloc\n",
                "#define while(condition) while ((sequel_work_tick(1), (condition)))\n",
            ]
        )
    directives.extend(f"#define {name} {alias}\n" for name, alias in aliases.items())
    directives.append(f'#include "../{relative}"\n')
    directives.extend(f"#undef {name}\n" for name in aliases)
    if resource_scope:
        directives.extend(["#undef malloc\n", "#undef calloc\n", "#undef realloc\n", "#undef free\n", "#undef while\n"])
    return "".join(directives)


native_library_data = "".join(native_include(path) for path in library_sources)
if not args.check:
    for path in generated.glob("native_lib_*.c"):
        path.unlink()
    native_library.write_text(native_library_data, encoding="utf-8", newline="\n")
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
