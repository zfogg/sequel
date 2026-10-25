"""Build the Python binding against this checkout's C library sources."""

from setuptools import Extension, setup

setup(
    ext_modules=[
        Extension(
            "sequel._sql",
            sources=["python/sequel/_sql.c", "src/sql/main.c", "src/utf8.c", "third_party/utf8proc.c"],
            include_dirs=["include", "third_party"],
            define_macros=[("UTF8PROC_STATIC", "1")],
        )
    ]
)
