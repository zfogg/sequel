.PHONY: sync format lint typecheck test check
sync:
	uv sync --extra test --locked
format:
	uv run --extra test python scripts/style.py --fix
lint:
	uv run --extra test python scripts/style.py
typecheck:
	uv run ty check
test:
	go test ./...
	go vet ./...
	uv run --extra test pytest -q
	cmake --build build --parallel 4
	ctest --test-dir build --output-on-failure --parallel 4
check: lint test
