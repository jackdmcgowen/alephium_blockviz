#!/usr/bin/env python3
"""Pack a VnV run directory into artifacts.zip for offline HTML regeneration.

Layout expected under --run-dir:
  run.json          (required)
  index.html        (optional at pack time)
  previous.json     (optional)
  matrix_table.html (optional; matrix runs)
  artifacts/        (case files; paths in run.json are relative to run-dir)

Zip includes run.json, previous.json, matrix_table.html, and everything under
artifacts/. Excludes artifacts.zip itself and nested zips.
"""
from __future__ import annotations

import argparse
import json
import sys
import zipfile
from pathlib import Path


def collect_files(run_dir: Path) -> list[Path]:
    files: list[Path] = []
    for name in ("run.json", "previous.json", "matrix_table.html", "index.html"):
        p = run_dir / name
        if p.is_file():
            files.append(p)
    art = run_dir / "artifacts"
    if art.is_dir():
        for p in sorted(art.rglob("*")):
            if p.is_file() and p.suffix != ".zip":
                files.append(p)
    return files


def pack(run_dir: Path, zip_path: Path | None = None) -> Path:
    run_dir = run_dir.resolve()
    if not (run_dir / "run.json").is_file():
        raise SystemExit(f"missing run.json in {run_dir}")
    zip_path = zip_path or (run_dir / "artifacts.zip")
    files = collect_files(run_dir)
    # Validate run.json paths when present
    try:
        doc = json.loads((run_dir / "run.json").read_text(encoding="utf-8"))
        for c in doc.get("cases") or []:
            for key in ("actual", "expected", "diff", "report"):
                rel = c.get(key)
                if not rel or not isinstance(rel, str):
                    continue
                # allow absolute only if exists; prefer relative
                p = Path(rel)
                if not p.is_absolute():
                    p = run_dir / rel
                if not p.is_file():
                    print(f"WARN: missing artifact for {c.get('id')}.{key}: {rel}", file=sys.stderr)
    except (OSError, json.JSONDecodeError) as e:
        print(f"WARN: could not validate run.json: {e}", file=sys.stderr)

    if zip_path.exists():
        zip_path.unlink()
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        for f in files:
            if f.resolve() == zip_path.resolve():
                continue
            arc = f.relative_to(run_dir).as_posix()
            zf.write(f, arcname=arc)
    print(f"Wrote {zip_path} ({zip_path.stat().st_size} bytes, {len(files)} files)")
    return zip_path


def main() -> int:
    ap = argparse.ArgumentParser(description="Pack VnV run dir into artifacts.zip")
    ap.add_argument("--run-dir", required=True, help="Path to run directory (has run.json)")
    ap.add_argument("--zip", default="", help="Output zip path (default: <run-dir>/artifacts.zip)")
    args = ap.parse_args()
    run_dir = Path(args.run_dir)
    zip_path = Path(args.zip) if args.zip else None
    pack(run_dir, zip_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
