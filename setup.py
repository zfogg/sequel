"""Build the Python binding against this checkout's C library sources."""

from pathlib import Path

from setuptools import Extension, setup

library_sources = sorted(str(path) for path in Path("lib").rglob("*.c") if path.as_posix() != "lib/analysis/tokens.c")

setup(
    ext_modules=[
        Extension(
            "sequel._sql",
            sources=[
                "python/sequel/_sql.c",
                *library_sources,
                "third_party/utf8proc/utf8proc.c",
            ],
            include_dirs=["include", "lib", "third_party/utf8proc"],
            define_macros=[("UTF8PROC_STATIC", "1")],
        )
    ]
)
