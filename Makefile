.PHONY: sync format format-python format-cx format-c format-cpp lint typecheck test check
sync:
	uv sync --extra test --locked
format:
	$(MAKE) format-python format-cx
format-python:
	uv run --extra test ruff format python tests scripts setup.py
	uv run --extra test ruff check --fix python tests scripts setup.py
format-cx: format-c format-cpp
format-c:
	git ls-files -- '*.c' '*.h' ':!*.cc' ':!*.cpp' ':!*.cxx' ':!*.hh' ':!*.hpp' ':!*.hxx' | xargs clang-format -i
format-cpp:
	git ls-files -- '*.cc' '*.cpp' '*.cxx' '*.hh' '*.hpp' '*.hxx' | xargs clang-format -i
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
