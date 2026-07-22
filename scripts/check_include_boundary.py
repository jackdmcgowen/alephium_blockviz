#!/usr/bin/env python3
"""Fail if app-layer sources pull Vulkan or graphics platform internals.

The product **app** MSVC project does not add Vulkan SDK includes. Including
`vulkan/vulkan.h` or `graphics/platform/gfx_platform.hpp` from `src/app/**`
breaks the Windows dual-track (C1083 / include isolation). See docs/platform.md.

Scans:
  src/app/**/*.{h,hpp,c,cpp}

Forbidden include path fragments (normalized /):
  vulkan/vulkan.h
  vulkan.h  (angle-bracket form without path)
  graphics/platform/gfx_platform.hpp
  graphics/graphics_system.hpp
  graphics/gpu_prv_lib.h

Allowed:
  graphics/gpu_pub_lib.h  (public, Vulkan-free)

Usage:
  python3 scripts/check_include_boundary.py
  python3 scripts/check_include_boundary.py --root .
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

# Normalized include paths that app code must never use.
FORBIDDEN = frozenset(
    {
        "vulkan/vulkan.h",
        "vulkan.h",
        "graphics/platform/gfx_platform.hpp",
        "graphics/graphics_system.hpp",
        "graphics/gpu_prv_lib.h",
        "gpu_prv_lib.h",
    }
)

INCLUDE_RE = re.compile(
    r'^\s*#\s*include\s*[<"]([^>"]+)[>"]',
    re.MULTILINE,
)


def normalize_include(inc: str) -> str:
    return inc.replace("\\", "/").strip()


def scan_file(path: Path, repo: Path) -> list[str]:
    errors: list[str] = []
    try:
        text = path.read_text(encoding="utf-8-sig", errors="replace")
    except OSError as e:
        return [f"{path}: cannot read: {e}"]

    rel = path.relative_to(repo).as_posix()
    for m in INCLUDE_RE.finditer(text):
        inc = normalize_include(m.group(1))
        # basename match for vulkan.h
        base = Path(inc).name
        if inc in FORBIDDEN or base in FORBIDDEN:
            errors.append(f"{rel}: forbidden include {inc!r}")
            continue
        # Also catch nested paths ending with forbidden basenames used as full paths
        if base == "vulkan.h" or base == "gfx_platform.hpp" or base == "gpu_prv_lib.h":
            if "gpu_pub_lib.h" in inc:
                continue
            # gfx_platform.hpp only forbidden under graphics/platform or as bare name
            if base == "gfx_platform.hpp" and (
                "graphics/platform" in inc or inc == "gfx_platform.hpp"
            ):
                errors.append(f"{rel}: forbidden include {inc!r}")
            elif base == "vulkan.h":
                errors.append(f"{rel}: forbidden include {inc!r}")
            elif base == "gpu_prv_lib.h":
                errors.append(f"{rel}: forbidden include {inc!r}")
    return errors


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--root", type=Path, default=Path.cwd())
    args = ap.parse_args()
    repo = args.root.resolve()
    app_root = repo / "src" / "app"
    if not app_root.is_dir():
        print(f"ERROR: no src/app under {repo}", file=sys.stderr)
        return 2

    all_err: list[str] = []
    for path in sorted(app_root.rglob("*")):
        if not path.is_file():
            continue
        if path.suffix.lower() not in {".h", ".hpp", ".c", ".cpp"}:
            continue
        all_err.extend(scan_file(path, repo))

    if all_err:
        print("Include-boundary check FAILED (app must stay Vulkan-free):")
        for e in all_err:
            print(f"  {e}")
        print(
            "\nRule: src/app/** must not include vulkan headers, gfx_platform.hpp, "
            "graphics_system.hpp, or gpu_prv_lib.h. Use gpu_pub_lib.h and forward decls "
            "(docs/platform.md)."
        )
        return 1

    print("Include-boundary check OK (src/app)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
