#!/usr/bin/env python3
"""Fail if product .cpp TUs under MSVC PCH projects omit the leading pch include.

Scans sln/*.vcxproj ClCompile entries that use PrecompiledHeader=Use (or inherit
project default Use) and verifies the source starts with:
  #include "<area>/pch.h"

Excludes: pch.cpp creators, lib/imgui/, pure .c files, and files marked NotUsing.

Usage:
  python3 scripts/check_pch.py
  python3 scripts/check_pch.py --root .
"""
from __future__ import annotations

import argparse
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

NS = {"ms": "http://schemas.microsoft.com/developer/msbuild/2003"}

# Project basename → expected pch include
PROJECT_PCH = {
    "alephium_visualizer": "app/pch.h",
    "graphics": "graphics/pch.h",
    "network": "network/pch.h",
    "blockviz_engine": "engine/pch.h",
    "int_visual": "app/pch.h",
    "bench_frame_profiler": "app/pch.h",
}


def local_name(tag: str) -> str:
    if tag.startswith("{"):
        return tag.rsplit("}", 1)[-1]
    return tag


def clcompile_pch_mode(elem: ET.Element, default: str = "Use") -> str:
    """Return Create / Use / NotUsing for a ClCompile item."""
    mode = default
    for child in elem:
        if local_name(child.tag) == "PrecompiledHeader" and child.text:
            mode = child.text.strip()
    return mode


def project_default_pch(root: ET.Element) -> str:
    # ItemDefinitionGroup ClCompile PrecompiledHeader
    for idg in root.iter():
        if local_name(idg.tag) != "ItemDefinitionGroup":
            continue
        for cl in idg:
            if local_name(cl.tag) != "ClCompile":
                continue
            for child in cl:
                if local_name(child.tag) == "PrecompiledHeader" and child.text:
                    return child.text.strip()
    return "Use"


def first_include(path: Path) -> str | None:
    try:
        text = path.read_text(encoding="utf-8-sig", errors="replace")
    except OSError as e:
        print(f"ERROR: cannot read {path}: {e}", file=sys.stderr)
        return None
    for line in text.splitlines():
        s = line.strip()
        if not s or s.startswith("//") or s.startswith("/*"):
            continue
        m = re.match(r'#\s*include\s*[<"]([^>"]+)[>"]', s)
        if m:
            return m.group(1).replace("\\", "/")
        # BOM or pragma once before include is ok to skip if only pragma
        if s.startswith("#pragma"):
            continue
        # Non-include first token → fail
        return s
    return None


def check_vcxproj(vcx: Path, repo: Path) -> list[str]:
    errors: list[str] = []
    name = vcx.stem
    expected = PROJECT_PCH.get(name)
    if not expected:
        return errors

    try:
        tree = ET.parse(vcx)
    except ET.ParseError as e:
        return [f"{vcx}: XML parse error: {e}"]

    root = tree.getroot()
    default_mode = project_default_pch(root)

    for item in root.iter():
        if local_name(item.tag) != "ClCompile":
            continue
        include = item.get("Include")
        if not include:
            continue
        rel = include.replace("\\", "/")
        if not rel.endswith(".cpp"):
            continue
        if "/imgui/" in rel or rel.startswith("..\\imgui") or "imgui/" in rel:
            continue
        if rel.endswith("pch.cpp"):
            continue

        mode = clcompile_pch_mode(item, default_mode)
        if mode == "NotUsing" or mode == "Create":
            continue
        if mode != "Use":
            continue

        src = (vcx.parent / include).resolve()
        if not src.is_file():
            # try repo-relative
            src = (repo / include.lstrip(".\\/").replace("\\", "/")).resolve()
        if not src.is_file():
            # common pattern ..\src\...
            cand = (vcx.parent / Path(include)).resolve()
            src = cand
        if not src.is_file():
            errors.append(f"{vcx.name}: missing source {include}")
            continue

        first = first_include(src)
        if first is None:
            errors.append(f"{src.relative_to(repo)}: empty or unreadable")
            continue
        if first != expected:
            errors.append(
                f"{src.relative_to(repo)}: first include is {first!r}, expected {expected!r} "
                f"(MSVC PCH=Use for {name})"
            )
    return errors


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--root", type=Path, default=Path.cwd())
    args = ap.parse_args()
    repo = args.root.resolve()
    sln = repo / "sln"
    if not sln.is_dir():
        print(f"ERROR: no sln/ under {repo}", file=sys.stderr)
        return 2

    all_err: list[str] = []
    for vcx in sorted(sln.glob("*.vcxproj")):
        if vcx.name.startswith("_"):
            continue
        all_err.extend(check_vcxproj(vcx, repo))

    if all_err:
        print("PCH check FAILED:")
        for e in all_err:
            print(f"  {e}")
        print(
            "\nRule: product .cpp with PrecompiledHeader=Use must start with "
            '#include "<area>/pch.h" (see AGENTS.md / docs/platform.md).'
        )
        return 1

    print("PCH check OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
