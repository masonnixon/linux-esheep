#!/usr/bin/env python3
"""Gate installation on the tracked files having explicit provenance."""
from pathlib import Path
import hashlib
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
CATALOG_ROOT = ROOT / "assets" / "Pets"
CONFIRMED_LICENSE_STATUS = "user-confirmed"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()

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
        if package.get("license_status") != CONFIRMED_LICENSE_STATUS:
            errors.append(f"manifest package {folder} lacks user-confirmed license status")

        package_dir = CATALOG_ROOT / folder
        xml_path = package_dir / "animations.xml"
        icon_path = package_dir / "icon.png"
        readme_path = package_dir / "README.md"
        for artifact in (xml_path, icon_path, readme_path):
            if not artifact.is_file():
                errors.append(f"catalog package {folder} is missing {artifact.name}")
        if xml_path.is_file() and sha256_file(xml_path) != package.get("xml_sha256"):
            errors.append(f"catalog package {folder} XML hash does not match manifest")
        if icon_path.is_file() and sha256_file(icon_path) != package.get("icon_sha256"):
            errors.append(f"catalog package {folder} icon hash does not match manifest")
        if readme_path.is_file() and sha256_file(readme_path) != package.get("readme_sha256"):
            errors.append(f"catalog package {folder} README hash does not match manifest")

    if errors:
        print("provenance check failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1
    print(f"provenance check passed: {len(packages)} upstream records and {len(REQUIRED)} tracked files")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
