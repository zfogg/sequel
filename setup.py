"""Build the Python binding against this checkout's C library sources."""

from setuptools import Extension, setup

setup(
    ext_modules=[
        Extension(
            "sequel._sql",
            sources=[
                "python/sequel/_sql.c",
                "src/sql/engine.c",
                "src/sql/source.c",
                "src/sql/dialect.c",
                "src/sql/lexer.c",
                "src/sql/parser.c",
                "src/sql/query.c",
                "src/sql/analysis_document.c",
                "src/sql/serialization.c",
                "src/sql/diagnostic.c",
                "src/sql/formatter.c",
                "src/sql/fingerprint.c",
                "src/sql/summary_document.c",
                "src/sql/sanitizer.c",
                "src/sql/diagnostics.c",
                "src/utf8.c",
                "third_party/utf8proc.c",
            ],
            include_dirs=["include", "third_party"],
            define_macros=[("UTF8PROC_STATIC", "1")],
        )
    ]
)
