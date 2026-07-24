#!/usr/bin/env python3
"""Generate an HTML VnV report from a run results JSON + case catalog.

Includes:
  - Pass/fail summary by suite and case
  - Case catalog descriptions
  - Expected vs actual (goldens / baselines)
  - Before/after comparison when a previous run JSON is provided

Usage:
  python3 scripts/generate_vnv_report.py \\
      --results vnv/reports/last_run.json \\
      --out vnv/reports/last_run.html

  python3 scripts/generate_vnv_report.py \\
      --results vnv/reports/last_run.json \\
      --before vnv/reports/previous_run.json \\
      --out vnv/reports/compare.html
"""
from __future__ import annotations

import argparse
import base64
import html
import json
import mimetypes
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


def load_json(path: Path) -> dict[str, Any] | list[Any] | None:
    if not path.is_file():
        return None
    try:
        with path.open(encoding="utf-8") as f:
            return json.load(f)
    except (OSError, json.JSONDecodeError) as e:
        print(f"WARN: failed to load {path}: {e}", file=sys.stderr)
        return None


def file_stat_line(path: Path) -> str:
    if not path.is_file():
        return "missing"
    st = path.stat()
    return f"{st.st_size} bytes"


def try_embed_image(path: Path, max_bytes: int = 2_000_000) -> str | None:
    """Return data-URI img src if file is a small enough image, else None."""
    if not path.is_file():
        return None
    if path.stat().st_size > max_bytes:
        return None
    mime, _ = mimetypes.guess_type(str(path))
    if mime not in ("image/png", "image/jpeg", "image/webp", "image/gif"):
        return None
    try:
        data = path.read_bytes()
    except OSError:
        return None
    b64 = base64.b64encode(data).decode("ascii")
    return f"data:{mime};base64,{b64}"


def esc(s: Any) -> str:
    return html.escape("" if s is None else str(s))


def status_class(status: str) -> str:
    s = (status or "").lower()
    if s in ("pass", "passed", "ok"):
        return "pass"
    if s in ("fail", "failed", "error"):
        return "fail"
    if s in ("skip", "skipped", "warn", "warning"):
        return "warn"
    return "unknown"


def metric_rows(expected: dict | None, actual: dict | None) -> str:
    """HTML table rows for bench metrics expected vs actual."""
    keys = [
        ("frame_ms.median", "frame median (ms)"),
        ("frame_ms.p95", "frame p95 (ms)"),
        ("cpu_ms.median", "cpu median (ms)"),
        ("gpu_ms.median", "gpu median (ms)"),
        ("confidence", "confidence"),
        ("total_blocks", "total blocks"),
        ("samples", "samples"),
    ]

    def dig(d: dict | None, dotted: str) -> Any:
        if not d:
            return None
        cur: Any = d
        for part in dotted.split("."):
            if not isinstance(cur, dict) or part not in cur:
                return None
            cur = cur[part]
        return cur

    rows = []
    for dotted, label in keys:
        exp_v = dig(expected, dotted)
        act_v = dig(actual, dotted)
        delta = ""
        cls = ""
        if exp_v is not None and act_v is not None:
            try:
                de = float(act_v) - float(exp_v)
                delta = f"{de:+.4f}" if abs(de) < 1000 else f"{de:+.1f}"
                # Flag regressions for timing (higher worse); confidence lower worse
                if "confidence" in dotted:
                    if float(act_v) + 1e-6 < float(exp_v) * 0.9:
                        cls = "regressed"
                    elif float(act_v) > float(exp_v):
                        cls = "improved"
                elif "total_blocks" in dotted or "samples" in dotted:
                    cls = ""
                else:
                    if float(act_v) > float(exp_v) * 1.15:
                        cls = "regressed"
                    elif float(act_v) < float(exp_v) * 0.95:
                        cls = "improved"
            except (TypeError, ValueError):
                pass
        rows.append(
            f"<tr class='{cls}'><td>{esc(label)}</td>"
            f"<td class='num'>{esc(exp_v if exp_v is not None else '—')}</td>"
            f"<td class='num'>{esc(act_v if act_v is not None else '—')}</td>"
            f"<td class='num'>{esc(delta or '—')}</td></tr>"
        )
    return "\n".join(rows)


def before_after_status(before_cases: dict[str, Any], case_id: str, status: str) -> str:
    prev = before_cases.get(case_id)
    if not prev:
        return "<span class='muted'>no prior</span>"
    prev_s = (prev.get("status") or "").lower()
    cur_s = (status or "").lower()
    if prev_s == cur_s:
        return f"<span class='same'>{esc(prev_s)} → {esc(cur_s)}</span>"
    if prev_s in ("fail", "failed") and cur_s in ("pass", "passed", "ok"):
        return f"<span class='improved'>{esc(prev_s)} → {esc(cur_s)}</span>"
    if prev_s in ("pass", "passed", "ok") and cur_s in ("fail", "failed", "error"):
        return f"<span class='regressed'>{esc(prev_s)} → {esc(cur_s)}</span>"
    return f"<span>{esc(prev_s)} → {esc(cur_s)}</span>"


def render_case_section(
    repo: Path,
    case_result: dict[str, Any],
    catalog_by_id: dict[str, Any],
    before_cases: dict[str, Any],
) -> str:
    cid = case_result.get("id") or case_result.get("case") or "?"
    status = case_result.get("status", "unknown")
    sc = status_class(status)
    cat = catalog_by_id.get(cid, {})
    desc = case_result.get("description") or cat.get("description") or ""
    kind = case_result.get("kind") or cat.get("kind") or ""
    suite = case_result.get("suite") or cat.get("suite") or ""
    message = case_result.get("message") or case_result.get("detail") or ""

    expected_path = case_result.get("expected") or cat.get("expected")
    actual_path = case_result.get("actual") or cat.get("actual")
    if case_result.get("headless") and cat.get("expected_headless"):
        expected_path = cat.get("expected_headless")
    diff_path = case_result.get("diff") or cat.get("diff")
    report_path = case_result.get("report") or cat.get("report")

    parts = [
        f"<section class='case {sc}' id='case-{esc(cid)}'>",
        f"<h3><span class='badge {sc}'>{esc(status.upper())}</span> {esc(cid)}</h3>",
        f"<p class='meta'>suite=<code>{esc(suite)}</code> kind=<code>{esc(kind)}</code></p>",
    ]
    if desc:
        parts.append(f"<p class='desc'>{esc(desc)}</p>")
    if message:
        parts.append(f"<p class='msg'><code>{esc(message)}</code></p>")

    # Before/after status transition
    if before_cases:
        parts.append(
            f"<p class='before-after'><strong>Before → after:</strong> "
            f"{before_after_status(before_cases, cid, status)}</p>"
        )

    # Paths table
    parts.append("<table class='paths'><thead><tr><th>Role</th><th>Path</th><th>Status</th></tr></thead><tbody>")
    for role, p in (
        ("expected (golden/baseline)", expected_path),
        ("actual", actual_path),
        ("diff", diff_path),
        ("compare report", report_path),
    ):
        if not p:
            continue
        pp = repo / p if not Path(p).is_absolute() else Path(p)
        exists = "present" if pp.is_file() else "missing"
        parts.append(
            f"<tr><td>{esc(role)}</td><td><code>{esc(p)}</code></td>"
            f"<td class='{exists}'>{esc(exists)} ({esc(file_stat_line(pp))})</td></tr>"
        )
    parts.append("</tbody></table>")

    # Visual goldens: embed expected / actual / diff thumbnails when small
    if kind == "visual_golden":
        parts.append("<div class='imgs'>")
        for label, p in (("Expected (golden)", expected_path), ("Actual", actual_path), ("Diff", diff_path)):
            if not p:
                continue
            pp = repo / p if not Path(p).is_absolute() else Path(p)
            uri = try_embed_image(pp)
            if uri:
                parts.append(
                    f"<figure><figcaption>{esc(label)}</figcaption>"
                    f"<img src='{uri}' alt='{esc(label)}' loading='lazy'/></figure>"
                )
            elif pp.is_file():
                parts.append(
                    f"<figure><figcaption>{esc(label)}</figcaption>"
                    f"<p class='muted'>(image too large to embed; see {esc(p)})</p></figure>"
                )
            else:
                parts.append(
                    f"<figure><figcaption>{esc(label)}</figcaption>"
                    f"<p class='muted'>missing</p></figure>"
                )
        parts.append("</div>")
        # Text report from compare_images
        if report_path:
            rp = repo / report_path if not Path(report_path).is_absolute() else Path(report_path)
            if rp.is_file():
                try:
                    text = rp.read_text(encoding="utf-8", errors="replace")
                except OSError:
                    text = ""
                if text.strip():
                    parts.append(f"<pre class='report-txt'>{esc(text)}</pre>")

    # Bench: expected (baseline) vs actual metrics
    if kind == "perf_baseline":
        exp_obj = None
        act_obj = None
        if expected_path:
            ep = repo / expected_path if not Path(expected_path).is_absolute() else Path(expected_path)
            exp_obj = load_json(ep) if ep.is_file() else None
        if actual_path:
            ap = repo / actual_path if not Path(actual_path).is_absolute() else Path(actual_path)
            act_obj = load_json(ap) if ap.is_file() else None
        # Prefer metrics embedded in run result
        if case_result.get("metrics"):
            act_obj = case_result["metrics"]
        if case_result.get("baseline"):
            exp_obj = case_result["baseline"]

        parts.append(
            "<table class='metrics'><thead><tr>"
            "<th>Metric</th><th>Expected (baseline)</th><th>Actual</th><th>Δ</th>"
            "</tr></thead><tbody>"
        )
        parts.append(metric_rows(exp_obj if isinstance(exp_obj, dict) else None,
                                 act_obj if isinstance(act_obj, dict) else None))
        parts.append("</tbody></table>")

        # Before/after metrics if previous run had metrics
        prev = before_cases.get(cid) if before_cases else None
        if prev and (prev.get("metrics") or prev.get("actual")):
            prev_act = prev.get("metrics")
            if not prev_act and prev.get("actual"):
                pap = repo / prev["actual"] if not Path(prev["actual"]).is_absolute() else Path(prev["actual"])
                prev_act = load_json(pap)
            if isinstance(prev_act, dict) and isinstance(act_obj, dict):
                parts.append("<h4>Before / after (previous run vs this run)</h4>")
                parts.append(
                    "<table class='metrics'><thead><tr>"
                    "<th>Metric</th><th>Before (prev actual)</th><th>After (this actual)</th><th>Δ</th>"
                    "</tr></thead><tbody>"
                )
                parts.append(metric_rows(prev_act, act_obj))
                parts.append("</tbody></table>")

        # Show raw JSON snippets (collapsed)
        for label, obj in (("Baseline JSON", exp_obj), ("Actual JSON", act_obj)):
            if isinstance(obj, dict):
                pretty = json.dumps(obj, indent=2)
                if len(pretty) > 8000:
                    pretty = pretty[:8000] + "\n… (truncated)"
                parts.append(
                    f"<details><summary>{esc(label)}</summary>"
                    f"<pre>{esc(pretty)}</pre></details>"
                )

    parts.append("</section>")
    return "\n".join(parts)


CSS = """
:root {
  --bg: #0f1419;
  --panel: #1a2332;
  --text: #e7ecf3;
  --muted: #8b9bb4;
  --pass: #3dd68c;
  --fail: #f07178;
  --warn: #e6b450;
  --border: #2a3548;
  --improved: #7fd99a;
  --regressed: #ff8b8b;
}
* { box-sizing: border-box; }
body {
  font-family: "Segoe UI", system-ui, -apple-system, sans-serif;
  background: var(--bg);
  color: var(--text);
  margin: 0;
  padding: 1.5rem 2rem 3rem;
  line-height: 1.45;
}
h1 { font-size: 1.6rem; margin: 0 0 0.25rem; }
h2 { font-size: 1.2rem; margin-top: 2rem; border-bottom: 1px solid var(--border); padding-bottom: 0.35rem; }
h3 { font-size: 1.05rem; margin: 0 0 0.5rem; }
h4 { font-size: 0.95rem; color: var(--muted); }
.sub { color: var(--muted); margin-bottom: 1.25rem; }
.summary {
  display: flex; flex-wrap: wrap; gap: 0.75rem; margin: 1rem 0 1.5rem;
}
.card {
  background: var(--panel);
  border: 1px solid var(--border);
  border-radius: 8px;
  padding: 0.75rem 1.1rem;
  min-width: 7rem;
}
.card .n { font-size: 1.6rem; font-weight: 700; }
.card.pass .n { color: var(--pass); }
.card.fail .n { color: var(--fail); }
.card.warn .n { color: var(--warn); }
.card .l { font-size: 0.75rem; color: var(--muted); text-transform: uppercase; letter-spacing: 0.04em; }
table { width: 100%; border-collapse: collapse; margin: 0.75rem 0; font-size: 0.9rem; }
th, td { border: 1px solid var(--border); padding: 0.4rem 0.55rem; text-align: left; }
th { background: #121a26; color: var(--muted); font-weight: 600; }
td.num { font-variant-numeric: tabular-nums; font-family: ui-monospace, monospace; }
tr.regressed td { background: rgba(240, 113, 120, 0.12); }
tr.improved td { background: rgba(61, 214, 140, 0.1); }
.badge {
  display: inline-block;
  font-size: 0.7rem;
  font-weight: 700;
  padding: 0.15rem 0.45rem;
  border-radius: 4px;
  vertical-align: middle;
  margin-right: 0.35rem;
  letter-spacing: 0.03em;
}
.badge.pass { background: rgba(61,214,140,0.2); color: var(--pass); }
.badge.fail { background: rgba(240,113,120,0.2); color: var(--fail); }
.badge.warn { background: rgba(230,180,80,0.2); color: var(--warn); }
.badge.unknown { background: #333; color: #aaa; }
.case {
  background: var(--panel);
  border: 1px solid var(--border);
  border-left: 4px solid var(--border);
  border-radius: 8px;
  padding: 1rem 1.15rem;
  margin: 1rem 0;
}
.case.pass { border-left-color: var(--pass); }
.case.fail { border-left-color: var(--fail); }
.case.warn { border-left-color: var(--warn); }
.meta, .msg { font-size: 0.85rem; color: var(--muted); }
.desc { margin: 0.4rem 0 0.6rem; }
code { font-family: ui-monospace, "Cascadia Code", monospace; font-size: 0.85em; }
.muted { color: var(--muted); }
.present { color: var(--pass); }
.missing { color: var(--fail); }
.improved { color: var(--improved); font-weight: 600; }
.regressed { color: var(--regressed); font-weight: 600; }
.same { color: var(--muted); }
.imgs {
  display: flex; flex-wrap: wrap; gap: 1rem; margin: 0.75rem 0;
}
.imgs figure {
  margin: 0; background: #0a0e14; border: 1px solid var(--border);
  border-radius: 6px; padding: 0.5rem; max-width: 420px;
}
.imgs figcaption { font-size: 0.75rem; color: var(--muted); margin-bottom: 0.35rem; }
.imgs img { max-width: 400px; max-height: 240px; width: auto; height: auto; display: block; border-radius: 3px; }
pre {
  background: #0a0e14;
  border: 1px solid var(--border);
  border-radius: 6px;
  padding: 0.75rem;
  overflow-x: auto;
  font-size: 0.8rem;
  max-height: 28rem;
}
.report-txt { max-height: 12rem; }
details { margin: 0.5rem 0; }
summary { cursor: pointer; color: var(--muted); font-size: 0.9rem; }
.toc a { color: #7eb8ff; text-decoration: none; }
.toc a:hover { text-decoration: underline; }
.toc li { margin: 0.2rem 0; }
footer { margin-top: 2.5rem; color: var(--muted); font-size: 0.8rem; }
"""


def build_html(
    repo: Path,
    results: dict[str, Any],
    catalog: dict[str, Any] | None,
    before: dict[str, Any] | None,
) -> str:
    cases = results.get("cases") or []
    catalog_by_id: dict[str, Any] = {}
    if catalog and isinstance(catalog.get("cases"), list):
        for c in catalog["cases"]:
            if isinstance(c, dict) and c.get("id"):
                catalog_by_id[c["id"]] = c

    before_cases: dict[str, Any] = {}
    if before and isinstance(before.get("cases"), list):
        for c in before["cases"]:
            if isinstance(c, dict):
                key = c.get("id") or c.get("case")
                if key:
                    before_cases[key] = c

    n_pass = sum(1 for c in cases if status_class(c.get("status", "")) == "pass")
    n_fail = sum(1 for c in cases if status_class(c.get("status", "")) == "fail")
    n_warn = sum(1 for c in cases if status_class(c.get("status", "")) == "warn")
    n_other = len(cases) - n_pass - n_fail - n_warn
    overall = results.get("status") or ("pass" if n_fail == 0 else "fail")

    generated = results.get("generated_at") or datetime.now(timezone.utc).isoformat()
    git_sha = results.get("git_sha") or ""
    headless = results.get("headless")
    config = results.get("config") or ""
    suites = results.get("suites") or []

    toc_items = []
    sections = []
    for c in cases:
        cid = c.get("id") or c.get("case") or "?"
        st = c.get("status", "?")
        toc_items.append(
            f"<li><a href='#case-{esc(cid)}'><span class='badge {status_class(st)}'>"
            f"{esc(st)}</span> {esc(cid)}</a></li>"
        )
        sections.append(render_case_section(repo, c, catalog_by_id, before_cases))

    # Catalog appendix for cases not run
    catalog_appendix = []
    run_ids = {c.get("id") or c.get("case") for c in cases}
    if catalog_by_id:
        for cid, cat in sorted(catalog_by_id.items()):
            if cid in run_ids:
                continue
            catalog_appendix.append(
                f"<tr><td><code>{esc(cid)}</code></td>"
                f"<td>{esc(cat.get('suite',''))}</td>"
                f"<td>{esc(cat.get('kind',''))}</td>"
                f"<td>{esc(cat.get('description',''))}</td></tr>"
            )

    before_note = ""
    if before:
        before_note = (
            f"<p class='sub'>Compared against previous run: "
            f"<code>{esc(before.get('generated_at', 'unknown'))}</code>"
            f"{(' · ' + esc(before.get('git_sha', ''))) if before.get('git_sha') else ''}"
            f"</p>"
        )

    body = f"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width, initial-scale=1"/>
<title>VnV Report — {esc(overall.upper())}</title>
<style>{CSS}</style>
</head>
<body>
<header>
  <h1>VnV Report <span class="badge {status_class(overall)}">{esc(overall.upper())}</span></h1>
  <p class="sub">
    Generated {esc(generated)}
    {f" · git <code>{esc(git_sha)}</code>" if git_sha else ""}
    {f" · config <code>{esc(config)}</code>" if config else ""}
    {f" · headless=<code>{esc(headless)}</code>" if headless is not None else ""}
    {f" · suites: {esc(', '.join(suites))}" if suites else ""}
  </p>
  {before_note}
</header>

<div class="summary">
  <div class="card pass"><div class="n">{n_pass}</div><div class="l">Passed</div></div>
  <div class="card fail"><div class="n">{n_fail}</div><div class="l">Failed</div></div>
  <div class="card warn"><div class="n">{n_warn}</div><div class="l">Warn / skip</div></div>
  <div class="card"><div class="n">{len(cases)}</div><div class="l">Total cases</div></div>
  {f'<div class="card"><div class="n">{n_other}</div><div class="l">Other</div></div>' if n_other else ''}
</div>

<h2>Case index</h2>
<ul class="toc">
{chr(10).join(toc_items) if toc_items else "<li class='muted'>No cases in this run</li>"}
</ul>

<h2>Results</h2>
{chr(10).join(sections) if sections else "<p class='muted'>No case results.</p>"}

{"<h2>Catalog (not run this session)</h2>" if catalog_appendix else ""}
{f'''<table>
<thead><tr><th>Id</th><th>Suite</th><th>Kind</th><th>Description</th></tr></thead>
<tbody>
{chr(10).join(catalog_appendix)}
</tbody></table>''' if catalog_appendix else ""}

<footer>
  Generated by <code>scripts/generate_vnv_report.py</code> ·
  Catalog: <code>vnv/manifest/case_catalog.json</code> ·
  Results schema: cases[].id/status/kind/expected/actual + optional metrics
</footer>
</body>
</html>
"""
    return body


def main() -> int:
    ap = argparse.ArgumentParser(description="Generate HTML VnV report from run JSON")
    ap.add_argument("--results", required=True, help="Path to last_run.json (or any results JSON)")
    ap.add_argument("--before", default="", help="Optional previous run JSON for before/after")
    ap.add_argument("--catalog", default="vnv/manifest/case_catalog.json")
    ap.add_argument("--out", default="vnv/reports/last_run.html")
    ap.add_argument("--repo-root", default="", help="Repo root (default: parent of scripts/)")
    args = ap.parse_args()

    script_dir = Path(__file__).resolve().parent
    repo = Path(args.repo_root).resolve() if args.repo_root else script_dir.parent

    results_path = Path(args.results)
    if not results_path.is_absolute():
        results_path = repo / results_path
    results = load_json(results_path)
    if not isinstance(results, dict):
        print(f"ERROR: results not found or invalid: {results_path}", file=sys.stderr)
        return 2

    catalog_path = Path(args.catalog)
    if not catalog_path.is_absolute():
        catalog_path = repo / catalog_path
    catalog = load_json(catalog_path)
    if not isinstance(catalog, dict):
        catalog = {"cases": []}

    before = None
    if args.before:
        before_path = Path(args.before)
        if not before_path.is_absolute():
            before_path = repo / before_path
        before = load_json(before_path)
        if not isinstance(before, dict):
            print(f"WARN: --before unreadable: {before_path}", file=sys.stderr)
            before = None

    out_path = Path(args.out)
    if not out_path.is_absolute():
        out_path = repo / out_path
    out_path.parent.mkdir(parents=True, exist_ok=True)

    html_doc = build_html(repo, results, catalog, before)
    out_path.write_text(html_doc, encoding="utf-8")
    print(f"Wrote {out_path} ({out_path.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
