import json
import re
from html import unescape
from pathlib import Path

import pytest
from sequel import Appearance, Dialect, HighlightMode, colorschemes, format_sql, highlight_sql

ROOT = Path(__file__).resolve().parents[2]


@pytest.mark.parametrize("mode", [HighlightMode.HTML, HighlightMode.ANSI])
@pytest.mark.parametrize("appearance", [Appearance.AUTO, Appearance.LIGHT, Appearance.DARK])
def test_source_and_colors(mode, appearance):
    sql = "SELECT :id, sum(value::int), '<script>&', true, null FROM 日本語\r\n-- comment\n"
    result = highlight_sql(
        sql, dialect=Dialect.POSTGRESQL, mode=mode, appearance=appearance, colors={"variable": "#123000"}
    )
    if mode == HighlightMode.HTML:
        assert unescape(re.sub(r"<[^>]*>", "", result)) == sql
        assert 'color:#123000">:id' in result
        assert "<script>" not in result
    else:
        assert re.sub(r"\x1b\[[0-9;]*m", "", result) == sql
        assert "\x1b[38;2;18;48;0m:id" in result
        assert result.endswith("\x1b[0m")


def test_registry_and_yaml():
    registry = json.loads((ROOT / "third_party/base16.json").read_text(encoding="utf-8"))
    assert colorschemes() == tuple(p["name"] for p in registry["schemes"])
    for palette in registry["schemes"]:
        for mode in (HighlightMode.HTML, HighlightMode.ANSI):
            assert highlight_sql("SELECT :id", mode=mode, colorscheme=palette["name"]) == highlight_sql(
                "SELECT :id", mode=mode, scheme_yaml=palette["yaml"]
            )
        for appearance in ("dark", "light"):
            variant = palette[appearance]
            if variant is None:
                with pytest.raises(ValueError, match="no published variant"):
                    highlight_sql("SELECT :id", colorscheme=palette["name"], appearance=appearance)
            else:
                for mode in ("html", "ansi"):
                    assert highlight_sql(
                        "SELECT :id", mode=mode, colorscheme=palette["name"], appearance=appearance
                    ) == highlight_sql("SELECT :id", mode=mode, colorscheme=variant)


@pytest.mark.parametrize("mode", ["html", "ansi"])
@pytest.mark.parametrize("appearance", ["dark", "light"])
def test_pipeline_samples(mode, appearance):
    directory = ROOT / "examples/highlight"
    sql = format_sql((directory / "query.sql").read_text(encoding="utf-8"), dialect=Dialect.POSTGRESQL)
    result = highlight_sql(sql, dialect=Dialect.POSTGRESQL, mode=mode, appearance=appearance)
    assert result.encode() == (directory / f"{appearance}.{mode}").read_bytes()


@pytest.mark.parametrize(
    "options",
    [
        {"mode": "bad"},
        {"appearance": "bad"},
        {"colorscheme": "bad"},
        {"scheme_yaml": "bad"},
        {"scheme_yaml": ""},
        {"colorscheme": "monokai", "scheme_yaml": "x"},
        {"colors": {"unknown": "#123000"}},
        {"colors": {"variable": "red"}},
        {"colors": {"variable": "#123000\x00"}},
        {"colors": {"variable\x00": "#123000"}},
        {"colorscheme": "default-dark\x00"},
        {"dialect": 99},
    ],
)
def test_invalid_options(options):
    with pytest.raises(ValueError):
        highlight_sql("SELECT 1", **options)


@pytest.mark.parametrize(
    "sql",
    ["SELECT '\x1b[2J'", "select '\x07'", "select 'unterminated", "/* incomplete", "a\x00b", "x " * 65537 + "'broken"],
    ids=["escape", "bell", "string", "comment", "nul", "oversize"],
)
def test_invalid_sql(sql):
    with pytest.raises(ValueError):
        highlight_sql(sql)


@pytest.mark.parametrize("color_value", [123, None, True, [], {}])
def test_invalid_color_value_types(color_value):
    with pytest.raises(TypeError):
        highlight_sql("SELECT 1", colors={"variable": color_value})
