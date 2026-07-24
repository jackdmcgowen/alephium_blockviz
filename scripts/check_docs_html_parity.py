#!/usr/bin/env python3
"""Fail if any docs/**/*.md lacks a sibling .html."""
from __future__ import annotations

import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
DOCS = REPO / "docs"


def main() -> int:
    missing = []
    for md in sorted(DOCS.rglob("*.md")):
        html = md.with_suffix(".html")
        if not html.is_file():
            missing.append(str(md.relative_to(REPO)))
    if missing:
        print("Missing HTML for Markdown sources:", file=sys.stderr)
        for m in missing:
            print(f"  {m}", file=sys.stderr)
        print("\nRun: python scripts/md_to_docs_html.py", file=sys.stderr)
        return 1
    print(f"OK: all {sum(1 for _ in DOCS.rglob('*.md'))} docs/**/*.md have sibling .html")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
