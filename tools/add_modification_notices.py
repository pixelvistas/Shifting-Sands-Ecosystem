#!/usr/bin/env python3
"""
add_modification_notices.py

Inserts GPLv2 section 2(a) modification notices into the files this fork
changed. Section 2(a) requires that modified files "carry prominent notices
stating that you changed the files and the date of any change."

Upstream headers are left intact; the notice is inserted immediately after the
existing copyright line so that provenance reads top to bottom:

    Copyright (c) 2016-2017 Thomas Wolf and Rasmus R. Paulsen (...)
    Modified 2026 by <name> (Shifting Sands - Ecosystem, York University).
      <what changed in this file>

Usage:
    python3 tools/add_modification_notices.py --name "Your Name"
    python3 tools/add_modification_notices.py --name "Your Name" --dry-run
    python3 tools/add_modification_notices.py --name "Your Name" --year 2026

Idempotent: files already carrying a notice are skipped.
"""

import argparse
import sys
from pathlib import Path

MARKER = "Modified 20"

# Per-file change summaries, drawn from PATCHES.md. Keep these specific --
# "ported to 0.12.1" on twelve files tells a reader nothing, and the point of
# the notice is that a reader can see what you did without a diff.
CHANGES = {
    "src/main.cpp": [
        "ofWindowSettings width/height replaced with setSize()/getWidth()/getHeight()",
        "for openFrameworks 0.12.1.",
    ],
    "src/ofApp.h": [
        "openFrameworks 0.9.3 -> 0.12.1 port; ofxDatGui replaced with ofxImGui.",
    ],
    "src/ofApp.cpp": [
        "openFrameworks 0.9.3 -> 0.12.1 port; ofxDatGui replaced with ofxImGui;",
        "GUI dispatch reworked for immediate mode.",
    ],
    "src/KinectProjector/KinectProjector.h": [
        "C++17 compatibility shim for std::binary_function; ofxDatGui -> ofxImGui;",
        "ofXml 0.9.3 -> 0.12.1 API port.",
    ],
    "src/KinectProjector/KinectProjector.cpp": [
        "C++17 compatibility shim for std::binary_function; ofxDatGui -> ofxImGui",
        "(immediate-mode drawGui); StubModal replaced with ImGui popups for",
        "calibration prompts; ofXml 0.9.3 -> 0.12.1 API port; std::min/max",
        "qualification; added buffer/ROI snapshot guards in filter(),",
        "applySpaceFilter() and updateGradientField(); normalised chessboard",
        "corner ordering in CalibrateNextPoint() (calibration correctness fix);",
        "fixed savePointPair() filename bug.",
    ],
    "src/KinectProjector/KinectGrabber.cpp": [
        "Removed std::move on filtered.send(), fixing a producer/consumer buffer",
        "race; added <algorithm> include; buffer reallocation after kinect.open().",
    ],
    "src/KinectProjector/KinectProjectorCalibration.h": [
        "C++17 compatibility shim; ofXml 0.9.3 -> 0.12.1 API port.",
    ],
    "src/KinectProjector/KinectProjectorCalibration.cpp": [
        "C++17 compatibility shim; ofXml 0.9.3 -> 0.12.1 API port.",
    ],
    "src/SandSurfaceRenderer/SandSurfaceRenderer.cpp": [
        "ofXml 0.9.3 -> 0.12.1 API port; addTexCoord replaced with glm::vec2;",
        "ofxDatGui GUI stripped.",
    ],
    "src/Games/ReferenceMapHandler.cpp": [
        "ofXml 0.9.3 -> 0.12.1 API port.",
    ],
    "src/Games/SandboxScoreTracker.cpp": [
        "ofXml 0.9.3 -> 0.12.1 API port.",
    ],
}

# Files whose headers should be checked manually if they were also touched.
ALSO_CHECK = [
    "src/KinectProjector/KinectGrabber.h",
    "src/SandSurfaceRenderer/SandSurfaceRenderer.h",
    "src/Games/ReferenceMapHandler.h",
    "src/Games/SandboxScoreTracker.h",
]


def build_notice(name, year, lines):
    out = [f"Modified {year} by {name} (Shifting Sands - Ecosystem, York University)."]
    out.extend(f"  {ln}" for ln in lines)
    return out


def process(path: Path, key: str, name: str, year: str, dry_run: bool) -> str:
    if not path.exists():
        return "missing"

    text = path.read_text(encoding="utf-8", errors="surrogateescape")

    if MARKER in text:
        return "already-noticed"

    lines = text.split("\n")

    # Find the last line of the upstream copyright block. Insert after it so
    # the original attribution stays first.
    insert_at = None
    for i, line in enumerate(lines[:40]):
        if line.strip().startswith("Copyright (c)"):
            insert_at = i + 1

    if insert_at is None:
        return "no-copyright-line"

    notice = build_notice(name, year, CHANGES.get(key, ["Modified for this fork."]))

    lines[insert_at:insert_at] = notice

    if not dry_run:
        path.write_text("\n".join(lines), encoding="utf-8", errors="surrogateescape")

    return "updated"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--name", required=True, help="Your name, as it should appear")
    ap.add_argument("--year", default="2026")
    ap.add_argument("--root", default=".", help="Repository root")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    root = Path(args.root).resolve()
    results = {}

    for rel in CHANGES:
        results[rel] = process(root / rel, rel, args.name, args.year, args.dry_run)

    width = max(len(r) for r in results) + 2
    for rel, status in sorted(results.items()):
        print(f"  {rel:<{width}} {status}")

    print()
    updated = sum(1 for s in results.values() if s == "updated")
    print(f"{updated} file(s) {'would be ' if args.dry_run else ''}updated.")

    missing = [r for r, s in results.items() if s == "missing"]
    if missing:
        print("\nNot found (check paths):")
        for m in missing:
            print(f"  {m}")

    print("\nAlso review these by hand if you changed them:")
    for f in ALSO_CHECK:
        print(f"  {f}")

    print(
        "\nNew files you authored (src/Sandbox/*) should carry your copyright\n"
        "as the primary holder, with a note that they derive from the Magic Sand\n"
        "module structure. They are not handled by this script."
    )

    if any(s == "no-copyright-line" for s in results.values()):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
