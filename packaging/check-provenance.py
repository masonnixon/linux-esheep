#!/usr/bin/env python3
"""Gate installation on the tracked files having explicit provenance."""
from pathlib import Path
import json
import sys

ROOT = Path(__file__).resolve().parent.parent
REQUIRED = {
    "assets/sheep_spritesheet.png",
    "assets/penguin_ice_blue_spritesheet.png",
    "assets/penguin_sheet_preview.png",
    "assets/penguin_ice_blue_spritesheet.md",
    "tools/esheep_animations.xml",
    "manifest.json",
    "NOTICE.md",
    "packaging/PROVENANCE.md",
}

def main() -> int:
    errors = []
    for relative in sorted(REQUIRED):
        path = ROOT / relative
        if not path.is_file():
            errors.append(f"missing required provenance-backed file: {relative}")

    try:
        manifest = json.loads((ROOT / "manifest.json").read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        errors.append(f"cannot read manifest.json for provenance validation: {exc}")
        manifest = {}

    packages = manifest.get("packages", [])
    if manifest.get("package_count") != len(packages):
        errors.append("manifest.json package_count does not match packages")
    for package in packages:
        folder = package.get("folder", "<unnamed>")
        if not package.get("attribution"):
            errors.append(f"manifest package {folder} has no attribution")
        if not package.get("upstream_revision"):
            errors.append(f"manifest package {folder} has no upstream revision")
        if not package.get("xml_sha256"):
            errors.append(f"manifest package {folder} has no XML hash")

    if errors:
        print("provenance check failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1
    print(f"provenance check passed: {len(packages)} upstream records and {len(REQUIRED)} tracked files")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
