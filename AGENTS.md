# Development checks

- Format C sources and headers with `clang-format`, and Go sources with `gofmt`.
- Use `uv` for Python tooling. Format Python with Ruff, lint with Ruff, and type-check with ty. Do not add type-checker suppression comments.
- Run `uv run python scripts/style.py --fix` after changes, then `uv run python scripts/style.py` to verify formatting, lint, types, generated palettes, and the native source fingerprint.
- Run `go vet ./...` for Go lint checks and the relevant C, Go, and Python tests before finishing.
- Keep Python implementations in dedicated modules; `__init__.py` should only re-export the public API.
- Document public C APIs with Doxygen comments and Python/Go APIs in their source. Build and validate hosted docs with `cmake -S docs -B build/docs` and `cmake --build build/docs --target docs`.
