"""Install a release wheel in isolation and run the complete Python binding suite."""

from __future__ import annotations

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("wheel", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="sequel-wheel-") as temporary:
        env = Path(temporary) / "venv"
        subprocess.run(["uv", "venv", "--python", sys.executable, str(env)], check=True)
        python = env / ("Scripts/python.exe" if (env / "Scripts").exists() else "bin/python")
        subprocess.run(
            ["uv", "pip", "install", "--python", str(python), str(args.wheel.resolve()), "pytest>=8"], check=True
        )
        subprocess.run(
            [
                str(python),
                "-I",
                "-c",
                "import pathlib,sequel,sequel._sql; "
                "assert pathlib.Path(sequel._sql.__file__).is_relative_to(pathlib.Path(__import__('sys').prefix)); "
                "assert sequel.analyze(\"SELECT 'private'\").text == 'SELECT ?'",
            ],
            cwd=temporary,
            check=True,
        )
        subprocess.run(
            [str(python), "-m", "pytest", "--import-mode=importlib", "-q", str(ROOT / "python/tests")],
            cwd=temporary,
            check=True,
        )


if __name__ == "__main__":
    main()
