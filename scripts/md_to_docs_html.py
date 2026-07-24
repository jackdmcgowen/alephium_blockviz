#!/usr/bin/env python3
"""Convert docs/**/*.md to sibling .html with shared site nav (stdlib only).

Usage (repo root):
  python scripts/md_to_docs_html.py              # all docs/**/*.md
  python scripts/md_to_docs_html.py docs/ROADMAP.md
"""
from __future__ import annotations

import html
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
DOCS = REPO / "docs"


def rel_to_docs_root(md_path: Path) -> str:
    rel = md_path.relative_to(DOCS)
    depth = len(rel.parts) - 1
    return "../" * depth if depth else ""


def title_from_md(text: str, fallback: str) -> str:
    for line in text.splitlines():
        if line.startswith("# "):
            return line[2:].strip()
    return fallback


def inline_format(s: str) -> str:
    s = html.escape(s)
    s = re.sub(r"`([^`]+)`", r"<code>\1</code>", s)
    s = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", s)
    s = re.sub(r"(?<!\*)\*([^*]+)\*(?!\*)", r"<em>\1</em>", s)
    s = re.sub(
        r"\[([^\]]+)\]\(([^)]+)\)",
        r'<a href="\2">\1</a>',
        s,
    )
    return s


def md_to_body(md: str) -> str:
    lines = md.splitlines()
    out: list[str] = []
    i = 0
    in_code = False
    code_lang = ""
    code_buf: list[str] = []
    in_ul = False
    in_table = False
    table_rows: list[list[str]] = []

    def close_ul():
        nonlocal in_ul
        if in_ul:
            out.append("</ul>")
            in_ul = False

    def flush_table():
        nonlocal in_table, table_rows
        if not table_rows:
            return
        out.append("<table>")
        for ri, row in enumerate(table_rows):
            # skip separator row |---|
            if all(re.match(r"^:?-+:?$", c.strip()) for c in row):
                continue
            tag = "th" if ri == 0 else "td"
            # if row 1 was separator, row 0 is header already handled
            cells = "".join(f"<{tag}>{inline_format(c.strip())}</{tag}>" for c in row)
            out.append(f"<tr>{cells}</tr>")
        out.append("</table>")
        table_rows = []
        in_table = False

    while i < len(lines):
        line = lines[i]

        if line.startswith("```"):
            close_ul()
            flush_table()
            if not in_code:
                in_code = True
                code_lang = line[3:].strip()
                code_buf = []
            else:
                out.append("<pre><code>" + html.escape("\n".join(code_buf)) + "</code></pre>")
                in_code = False
            i += 1
            continue

        if in_code:
            code_buf.append(line)
            i += 1
            continue

        if "|" in line and line.strip().startswith("|"):
            close_ul()
            cells = [c for c in line.strip().strip("|").split("|")]
            if not in_table:
                in_table = True
                table_rows = []
            table_rows.append(cells)
            i += 1
            # peek end of table
            if i >= len(lines) or "|" not in lines[i]:
                flush_table()
            continue

        if in_table:
            flush_table()

        if not line.strip():
            close_ul()
            i += 1
            continue

        if line.startswith("# "):
            close_ul()
            out.append(f"<h1>{inline_format(line[2:].strip())}</h1>")
        elif line.startswith("## "):
            close_ul()
            out.append(f"<h2>{inline_format(line[3:].strip())}</h2>")
        elif line.startswith("### "):
            close_ul()
            out.append(f"<h3>{inline_format(line[4:].strip())}</h3>")
        elif line.startswith("- ") or line.startswith("* "):
            if not in_ul:
                out.append("<ul>")
                in_ul = True
            out.append(f"<li>{inline_format(line[2:].strip())}</li>")
        elif re.match(r"^\d+\.\s", line):
            close_ul()
            out.append(f"<p>{inline_format(line.strip())}</p>")
        else:
            close_ul()
            out.append(f"<p>{inline_format(line.strip())}</p>")
        i += 1

    close_ul()
    flush_table()
    return "\n".join(out)


def nav_html(prefix: str, current: str) -> str:
    return f"""<nav class="site-nav" aria-label="Docs">
  <div class="site-nav-inner">
    <a class="brand" href="{prefix}index.html">BlockViz docs</a>
    <a href="{prefix}index.html">Hub</a>
    <a href="{prefix}layers/index.html">Layers</a>
    <a href="{prefix}ROADMAP.html">Roadmap</a>
    <a href="{prefix}planning/index.html">Planning</a>
    <a href="{prefix}user-guide-timeline.html">Timeline</a>
  </div>
</nav>"""


def convert_file(md_path: Path) -> Path:
    text = md_path.read_text(encoding="utf-8")
    title = title_from_md(text, md_path.stem)
    prefix = rel_to_docs_root(md_path)
    body = md_to_body(text)
    rel_md = md_path.name
    html_out = f"""<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>Alephium BlockViz — {html.escape(title)}</title>
  <link rel="stylesheet" href="{prefix}site.css" />
</head>
<body>
{nav_html(prefix, md_path.name)}
<main>
  <p class="muted">Source: <a href="{html.escape(rel_md)}"><code>{html.escape(rel_md)}</code></a></p>
{body}
</main>
</body>
</html>
"""
    html_path = md_path.with_suffix(".html")
    html_path.write_text(html_out, encoding="utf-8", newline="\n")
    return html_path


def main() -> int:
    args = [Path(a) for a in sys.argv[1:]]
    if args:
        paths = []
        for a in args:
            p = a if a.is_absolute() else REPO / a
            if p.is_dir():
                paths.extend(sorted(p.rglob("*.md")))
            else:
                paths.append(p)
    else:
        paths = sorted(DOCS.rglob("*.md"))

    n = 0
    for md in paths:
        if not md.exists() or md.suffix != ".md":
            print(f"skip missing {md}", file=sys.stderr)
            continue
        if "node_modules" in md.parts:
            continue
        out = convert_file(md)
        print(f"wrote {out.relative_to(REPO)}")
        n += 1
    print(f"done: {n} files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
