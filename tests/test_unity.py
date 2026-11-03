"""Compare SQL work accounting and allocator ABI across source layouts."""

import subprocess
import sys


def output(program: str) -> str:
    return subprocess.run([program], check=True, capture_output=True, text=True).stdout.strip()


unity, separate = (output(program) for program in sys.argv[1:])
assert unity.isdecimal() and unity == separate, (unity, separate)
