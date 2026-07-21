#!/usr/bin/env python3
"""Pixel-diff two PNGs (Linux / CI). Exit 0=pass, 1=fail, 2=IO/usage.

Mirrors compare_images.ps1 thresholds:
  per-channel max delta (default 8), max bad fraction (default 0.2%).

Requires Pillow:  pip install pillow   (or apt install python3-pil)
"""
from __future__ import annotations

import argparse
import sys


def main() -> int:
    ap = argparse.ArgumentParser(description="Compare two PNGs with per-channel tolerance")
    ap.add_argument("--expected", required=True)
    ap.add_argument("--actual", required=True)
    ap.add_argument("--diff-out", default="")
    ap.add_argument("--report-out", default="")
    ap.add_argument("--per-pixel-tol", type=int, default=8)
    ap.add_argument("--max-bad-fraction", type=float, default=0.002)
    args = ap.parse_args()

    try:
        from PIL import Image
    except ImportError:
        print("ERROR: Pillow required (pip install pillow / apt install python3-pil)", file=sys.stderr)
        return 2

    try:
        exp = Image.open(args.expected).convert("RGB")
        act = Image.open(args.actual).convert("RGB")
    except OSError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2

    if exp.size != act.size:
        print(f"FAIL: size mismatch expected={exp.size[0]}x{exp.size[1]} actual={act.size[0]}x{act.size[1]}")
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
            is_bad = d > args.per_pixel_tol
            if is_bad:
                bad += 1
            if diff_px is not None:
                v = min(255, d * 4)
                diff_px[x, y] = (v, 0, 0) if is_bad else (0, v, 0)

    frac = (bad / total) if total else 1.0
    report = "\n".join(
        [
            f"expected={args.expected}",
            f"actual={args.actual}",
            f"size={w}x{h}",
            f"per_pixel_tol={args.per_pixel_tol}",
            f"max_bad_fraction={args.max_bad_fraction}",
            f"bad_pixels={bad}",
            f"bad_fraction={frac:.6f}",
            f"max_channel_delta={max_delta}",
        ]
    )
    print(report)
    if args.report_out:
        with open(args.report_out, "w", encoding="utf-8") as f:
            f.write(report + "\n")
    if diff_img is not None and args.diff_out:
        diff_img.save(args.diff_out)

    if frac > args.max_bad_fraction:
        print(f"FAIL: bad_fraction {frac:.6f} > {args.max_bad_fraction}")
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
