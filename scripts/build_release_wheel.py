"""Use the selected native Python, not the repository's python/ directory."""

import subprocess
import sys

if sys.version_info[:2] != (3, 13):
    raise SystemExit("Release wheels require the selected native CPython 3.13 interpreter")
subprocess.run(["uv", "build", "--wheel", "--python", sys.executable, "--out-dir", "dist/raw"], check=True)
