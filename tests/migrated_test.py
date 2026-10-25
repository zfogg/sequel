"""Go regression tables and the complete SQLC corpus through the public C API."""

import json
import re
import subprocess
import sys
from pathlib import Path

root = Path(__file__).parent


def inspect(queries, dialect=2):
    data = "".join(f"{dialect} 0 0 0 {q.encode().hex()}\n" for q in queries)
    process = subprocess.run([sys.argv[1]], input=data, text=True, capture_output=True, check=True)
    rows = []
    for line in process.stdout.splitlines():
        status, truncated, count, operation, text, summary = line.split(" ")
        rows.append((int(status), bytes.fromhex(summary).decode(), bytes.fromhex(text).decode()))
    assert len(rows) == len(queries)
    return rows


cases = json.loads((root / "migrated_go.json").read_text(encoding="utf-8"))
assert len(cases) >= 50
for dialect in (0, 1, 2, 3, 4):
    selected = [c for c in cases if dialect != 4 or '"' not in c["query"]]
    for case, (_, summary, _) in zip(selected, inspect([c["query"] for c in selected], dialect)):
        assert summary == case["summary"], (dialect, case, summary)

joins = [
    "JOIN",
    "INNER JOIN",
    "LEFT JOIN",
    "LEFT OUTER JOIN",
    "RIGHT JOIN",
    "FULL OUTER JOIN",
    "CROSS JOIN",
    "NATURAL JOIN",
    "NATURAL LEFT JOIN",
    "SEMI JOIN",
    "ANTI JOIN",
    "ASOF LEFT JOIN",
    "POSITIONAL JOIN",
    "JOIN LATERAL",
    "LEFT JOIN LATERAL",
    "CROSS JOIN LATERAL",
]
queries = [
    f"SELECT * FROM accounts a {j} (SELECT * FROM accounts WHERE token='secret' LIMIT 1) b ON a.id=b.id LIMIT $1"
    for j in joins
]
for join, (status, summary, _) in zip(joins, inspect(queries, 3)):
    assert status == 0
    assert (
        summary
        == f"SELECT … FROM accounts AS a {join} (SELECT … FROM accounts WHERE … LIMIT 1) AS b ON a.id=b.id LIMIT ?"
    )

queries, labels = [], []
for file in sorted((root / "stonkpile").glob("*.sql")):
    sql = file.read_text(encoding="utf-8")
    matches = list(re.finditer(r"(?m)^-- name: ([A-Za-z0-9_]+) :[^\r\n]+", sql))
    for i, match in enumerate(matches):
        queries.append(sql[match.start() : matches[i + 1].start() if i + 1 < len(matches) else len(sql)])
        labels.append(f"{file.name} / {match[1]}")
assert len(queries) > 100
output = []
for label, (status, summary, safe) in zip(labels, inspect(queries)):
    assert status == 0 and summary not in ("SQL", "SELECT"), label
    for keyword in ("JOIN", "ON", "USING", "LATERAL", "LIMIT", "OFFSET", "UNION", "INTERSECT", "EXCEPT"):
        pattern = rf"\b{keyword}\b"
        assert len(re.findall(pattern, summary, re.I)) == len(re.findall(pattern, safe, re.I)), (label, keyword)
    output.append(f"{label} => {summary}\n")
assert "".join(output) == (root / "stonkpile_summaries.txt").read_text(encoding="utf-8")
print(f"Checked {len(cases)} migrated table cases, {len(joins)} join kinds and {len(queries)} named SQLC statements")
