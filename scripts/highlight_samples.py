"""Regenerate the checked-in HTML/ANSI examples using a built sequel executable."""

import argparse
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("binary", type=Path)
args = parser.parse_args()
binary = args.binary.resolve()
directory = Path(__file__).resolve().parents[1] / "examples/highlight"
sql = (directory / "query.sql").read_bytes()
formatted = subprocess.check_output([str(binary), "format", "--dialect=postgresql"], input=sql)
for appearance in ("dark", "light"):
    for mode in ("html", "ansi"):
        result = subprocess.check_output(
            [str(binary), "highlight", "--dialect=postgresql", f"--{mode}", f"--{appearance}"], input=formatted
        )
        (directory / f"{appearance}.{mode}").write_bytes(result)
