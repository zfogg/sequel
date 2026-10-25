"""Run the project's formatters, linters and type checker through uv."""

import argparse
import subprocess
from pathlib import Path

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--fix", action="store_true")
args = parser.parse_args()


def run(*command):
    subprocess.run(command, cwd=root, check=True)


go_files = sorted(str(p.relative_to(root)) for d in ("go", "cmd") for p in (root / d).rglob("*.go"))
c_files = sorted(
    str(p.relative_to(root))
    for d in ("src", "include", "tests", "python/sequel")
    for p in (root / d).rglob("*")
    if p.suffix in (".c", ".h")
)
if args.fix:
    run("gofmt", "-w", *go_files)
    run("clang-format", "-i", *c_files)
    run("ruff", "format", "python", "tests", "scripts", "setup.py")
    run("ruff", "check", "--fix", "python", "tests", "scripts", "setup.py")
    run("python", "scripts/sync_native.py")
else:
    result = subprocess.run(["gofmt", "-l", *go_files], cwd=root, text=True, capture_output=True, check=True)
    if result.stdout:
        raise SystemExit("Run gofmt on:\n" + result.stdout)
    run("clang-format", "--dry-run", "--Werror", *c_files)
    run("ruff", "format", "--check", "python", "tests", "scripts", "setup.py")
    run("ruff", "check", "python", "tests", "scripts", "setup.py")
    run("python", "scripts/sync_native.py", "--check")
run("ty", "check")
