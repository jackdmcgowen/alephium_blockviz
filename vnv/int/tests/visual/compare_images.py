#!/usr/bin/env python3
"""Pixel-diff two PNGs. Exit 0=pass, 1=fail, 2=IO/usage.

Profiles (multi-platform policy — see goldens/README.md):
  desktop   — tight thresholds (default): tol=8, bad=0.2%
  headless  — relaxed for lavapipe/GPU readback: tol=48, bad=5%
  smoke     — size + min bytes only (no Pillow required)

Requires Pillow for desktop/headless pixel compare:
  pip install pillow   /   apt install python3-pil
"""
from __future__ import annotations

import argparse
import struct
import sys
import zlib
from pathlib import Path


def png_size_stdlib(path: Path) -> tuple[int, int] | None:
    """Read IHDR width/height without Pillow."""
    try:
        data = path.read_bytes()
    except OSError:
        return None
    if len(data) < 24 or data[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    # first chunk should be IHDR
    if data[12:16] != b"IHDR":
        return None
    w, h = struct.unpack(">II", data[16:24])
    return int(w), int(h)


def main() -> int:
    ap = argparse.ArgumentParser(description="Compare two PNGs with per-channel tolerance")
    ap.add_argument("--expected", default="")
    ap.add_argument("--actual", required=True)
    ap.add_argument("--diff-out", default="")
    ap.add_argument("--report-out", default="")
    ap.add_argument("--profile", choices=("desktop", "headless", "smoke"), default="desktop")
    ap.add_argument("--per-pixel-tol", type=int, default=None)
    ap.add_argument("--max-bad-fraction", type=float, default=None)
    ap.add_argument("--require-width", type=int, default=1280)
    ap.add_argument("--require-height", type=int, default=720)
    ap.add_argument("--min-bytes", type=int, default=256)
    args = ap.parse_args()

    if args.profile == "desktop":
        tol = 8 if args.per_pixel_tol is None else args.per_pixel_tol
        bad_frac = 0.002 if args.max_bad_fraction is None else args.max_bad_fraction
    elif args.profile == "headless":
        tol = 48 if args.per_pixel_tol is None else args.per_pixel_tol
        bad_frac = 0.05 if args.max_bad_fraction is None else args.max_bad_fraction
    else:
        tol = args.per_pixel_tol
        bad_frac = args.max_bad_fraction

    actual = Path(args.actual)
    if not actual.is_file():
        print(f"ERROR: missing actual: {actual}", file=sys.stderr)
        return 2

    size = actual.stat().st_size
    if size < args.min_bytes:
        print(f"FAIL: actual too small ({size} < {args.min_bytes} bytes)")
        return 1

    dims = png_size_stdlib(actual)
    if dims is None:
        print("FAIL: actual is not a valid PNG with IHDR")
        return 1
    aw, ah = dims
    if args.require_width and aw != args.require_width:
        print(f"FAIL: width {aw} != required {args.require_width}")
        return 1
    if args.require_height and ah != args.require_height:
        print(f"FAIL: height {ah} != required {args.require_height}")
        return 1

    print(f"actual={actual} size_bytes={size} dims={aw}x{ah} profile={args.profile}")

    if args.profile == "smoke" or not args.expected:
        print("PASS (smoke: size/dims/min-bytes)")
        return 0

    expected = Path(args.expected)
    if not expected.is_file():
        print(f"ERROR: missing expected: {expected}", file=sys.stderr)
        return 2

    try:
        from PIL import Image
    except ImportError:
        print(
            "WARN: Pillow missing — smoke-only pass (install python3-pil for pixel compare)",
            file=sys.stderr,
        )
        print("PASS (smoke fallback without Pillow)")
        return 0

    try:
        exp = Image.open(expected).convert("RGB")
        act = Image.open(actual).convert("RGB")
    except OSError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2

    if exp.size != act.size:
        print(
            f"FAIL: size mismatch expected={exp.size[0]}x{exp.size[1]} "
            f"actual={act.size[0]}x{act.size[1]}"
        )
        return 1

    w, h = exp.size
    total = w * h
    bad = 0
    max_delta = 0
    exp_px = exp.load()
    act_px = act.load()
    diff_img = Image.new("RGB", (w, h)) if args.diff_out else None
    diff_px = diff_img.load() if diff_img else None

    for y in range(h):
        for x in range(w):
            r0, g0, b0 = exp_px[x, y]
            r1, g1, b1 = act_px[x, y]
            d = max(abs(r0 - r1), abs(g0 - g1), abs(b0 - b1))
            if d > max_delta:
                max_delta = d
            is_bad = d > (tol or 8)
            if is_bad:
                bad += 1
            if diff_px is not None:
                v = min(255, d * 4)
                diff_px[x, y] = (v, 0, 0) if is_bad else (0, v, 0)

    frac = (bad / total) if total else 1.0
    max_bad = bad_frac if bad_frac is not None else 0.002
    report = "\n".join(
        [
            f"expected={expected}",
            f"actual={actual}",
            f"size={w}x{h}",
            f"profile={args.profile}",
            f"per_pixel_tol={tol}",
            f"max_bad_fraction={max_bad}",
            f"bad_pixels={bad}",
            f"bad_fraction={frac:.6f}",
            f"max_channel_delta={max_delta}",
        ]
    )
    print(report)
    if args.report_out:
        Path(args.report_out).parent.mkdir(parents=True, exist_ok=True)
        Path(args.report_out).write_text(report + "\n", encoding="utf-8")
    if diff_img is not None and args.diff_out:
        Path(args.diff_out).parent.mkdir(parents=True, exist_ok=True)
        diff_img.save(args.diff_out)

    if frac > max_bad:
        print(f"FAIL: bad_fraction {frac:.6f} > {max_bad}")
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
