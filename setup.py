"""Build the Python binding against this checkout's C library sources."""

from setuptools import Extension, setup

setup(
    ext_modules=[
        Extension(
            "sequel._sql",
            sources=[
                "python/sequel/_sql.c",
                "lib/engine.c",
                "lib/runtime.c",
                "lib/contracts.c",
                "lib/sha256.c",
                "lib/highlight.c",
                "lib/compact.c",
                "lib/api.c",
                "lib/scan.c",
                "lib/analysis/analysis.c",
                "lib/analysis/analysis_literal.c",
                "lib/analysis/analysis_binding_token.c",
                "lib/analysis/analysis_text.c",
                "lib/analysis/analysis_policy.c",
                "lib/source.c",
                "lib/dialect.c",
                "lib/lexer.c",
                "lib/parser.c",
                "lib/query.c",
                "lib/diagnostics.c",
                "lib/analysis_document.c",
                "lib/serialization.c",
                "lib/formatter.c",
                "lib/fingerprint.c",
                "lib/summary/render.c",
                "lib/summary/buffer.c",
                "lib/summary/options.c",
                "lib/summary/lexical.c",
                "lib/summary/operation.c",
                "lib/summary/clause.c",
                "lib/summary/body.c",
                "lib/summary/relation.c",
                "lib/summary/postprocess.c",
                "lib/summary/statement.c",
                "lib/summary/subquery.c",
                "lib/sanitizer.c",
                "lib/util/utf8.c",
                "third_party/utf8proc/utf8proc.c",
            ],
            include_dirs=["include", "third_party/utf8proc"],
            define_macros=[("UTF8PROC_STATIC", "1")],
        )
    ]
)
