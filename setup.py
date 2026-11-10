"""Build the Python binding against this checkout's C library sources."""

from setuptools import Extension, setup

setup(
    ext_modules=[
        Extension(
            "sequel._sql",
            sources=[
                "python/sequel/_sql.c",
                "lib/engine.c",
                "lib/source.c",
                "lib/dialect.c",
                "lib/lexer.c",
                "lib/parser.c",
                "lib/query.c",
                "lib/analysis_document.c",
                "lib/serialization.c",
                "lib/formatter.c",
                "lib/fingerprint.c",
                "lib/summary/document.c",
                "lib/summary/buffer.c",
                "lib/summary/options.c",
                "lib/summary/lexical.c",
                "lib/sanitizer.c",
                "lib/util/utf8.c",
                "third_party/utf8proc/utf8proc.c",
            ],
            include_dirs=["include", "third_party/utf8proc"],
            define_macros=[("UTF8PROC_STATIC", "1")],
        )
    ]
)
