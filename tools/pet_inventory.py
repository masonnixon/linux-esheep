#!/usr/bin/env python3
"""Reproducible upstream pet package inventory and provenance tool.

Scans a directory of upstream pet packages (each with animations.xml,
icon.png, README.md) and emits a deterministic JSON manifest containing:
- package name / folder
- upstream revision (git commit of the reference checkout)
- animation / transition / child / spawn / sound counts
- image / tile metadata (dimensions, tiles, base64 vs file)
- SHA-256 hashes of XML, icon, README
- author / title / petname / version / license / attribution
- explicit unresolved-rights state when applicable

Designed to be run from the repo root or against any reference checkout.
All counts are derived from the XML, never hand-counted.
"""
import argparse
import hashlib
import json
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

NS = {"e": "https://esheep.petrucci.ch/"}
NS_URI = "https://esheep.petrucci.ch/"

# Upstream known authors / provenance for attribution status
UPSTREAM_AUTHORS = {
    "Adriano": "Project: https://github.com/Adrianotiger/desktopPet",
    "Oliver B.": "Upstream contributor",
    "RedSparr0w": "Upstream contributor",
    "Michelle!": "Upstream contributor (original art)",
    "Madnuttah": "Upstream contributor",
}

# License status: most upstream packages have no explicit license in the
# XML, so they remain "unresolved" until cleared.
DEFAULT_LICENSE_STATUS = "unresolved"


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_text(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def parse_header(header_el):
    """Extract header fields from the package XML."""
    if header_el is None:
        return {}
    fields = {}
    for tag in ("author", "title", "petname", "version", "info"):
        val = header_el.findtext(f"e:{tag}", "", NS)
        fields[tag] = val.strip() if val else ""
    return fields


def parse_image(image_el):
    """Extract image / sprite metadata."""
    if image_el is None:
        return {}
    info = {
        "tilesx": image_el.findtext("e:tilesx", "", NS),
        "tilesy": image_el.findtext("e:tilesy", "", NS),
        "transparency": image_el.findtext("e:transparency", "", NS),
        "has_base64_png": False,
        "has_file_ref": False,
        "has_spritesheet_ref": False,
    }
    # base64 embedded PNG
    png_el = image_el.find("e:png", NS)
    if png_el is not None and png_el.text and png_el.text.strip():
        info["has_base64_png"] = True
    # file reference
    file_el = image_el.find("e:file", NS)
    if file_el is not None and file_el.text and file_el.text.strip():
        info["has_file_ref"] = True
    # spritesheet reference
    ss_el = image_el.find("e:spritesheet", NS)
    if ss_el is not None and ss_el.text and ss_el.text.strip():
        info["has_spritesheet_ref"] = True
    return info


def count_transitions(root):
    """Count all <next> transition records (animation, border, gravity, spawn)."""
    total = 0
    # animation-level transitions
    for anim in root.findall("e:animations/e:animation", NS):
        total += len(anim.findall("e:sequence/e:next", NS))
        total += len(anim.findall("e:border/e:next", NS))
        total += len(anim.findall("e:gravity/e:next", NS))
        total += len(anim.findall("e:start/e:next", NS))
        total += len(anim.findall("e:end/e:next", NS))
    # spawn-level transitions
    for spawn in root.findall("e:spawns/e:spawn", NS):
        total += len(spawn.findall("e:next", NS))
    # child transitions
    for child in root.findall("e:childs/e:child", NS):
        total += len(child.findall("e:next", NS))
    return total


def parse_package(pet_dir: Path, ref_revision: str) -> dict:
    """Parse a single pet package directory and return provenance facts."""
    folder = pet_dir.name
    xml_path = pet_dir / "animations.xml"
    icon_path = pet_dir / "icon.png"
    readme_path = pet_dir / "README.md"

    result = {
        "folder": folder,
        "upstream_revision": ref_revision,
        "xml_sha256": None,
        "icon_sha256": None,
        "readme_sha256": None,
        "author": "",
        "title": "",
        "petname": "",
        "version": "",
        "license_status": DEFAULT_LICENSE_STATUS,
        "attribution": "",
        "tilesx": 0,
        "tilesy": 0,
        "animations_count": 0,
        "transitions_count": 0,
        "child_count": 0,
        "spawn_count": 0,
        "sound_count": 0,
        "has_sounds": False,
        "image_base64": False,
        "image_file_ref": False,
        "spritesheet_ref": False,
        "transparency": "",
        "errors": [],
    }

    if not xml_path.exists():
        result["errors"].append("missing animations.xml")
        return result

    xml_content = xml_path.read_bytes()
    result["xml_sha256"] = sha256_bytes(xml_content)

    if icon_path.exists():
        result["icon_sha256"] = sha256_bytes(icon_path.read_bytes())
    if readme_path.exists():
        result["readme_sha256"] = sha256_bytes(readme_path.read_bytes())

    try:
        root = ET.fromstring(xml_content)
    except ET.ParseError as e:
        result["errors"].append(f"XML parse error: {e}")
        return result

    header = root.find("e:header", NS)
    header_info = parse_header(header)
    result.update({
        "author": header_info.get("author", ""),
        "title": header_info.get("title", ""),
        "petname": header_info.get("petname", ""),
        "version": header_info.get("version", ""),
    })

    info = parse_image(root.find("e:image", NS))
    result.update({
        "tilesx": int(info["tilesx"]) if info["tilesx"] else 0,
        "tilesy": int(info["tilesy"]) if info["tilesy"] else 0,
        "image_base64": info["has_base64_png"],
        "image_file_ref": info["has_file_ref"],
        "spritesheet_ref": info["has_spritesheet_ref"],
        "transparency": info.get("transparency", ""),
    })

    # animation count
    animations = root.findall("e:animations/e:animation", NS)
    result["animations_count"] = len(animations)

    # child count
    childs = root.findall("e:childs/e:child", NS)
    result["child_count"] = len(childs)

    # spawn count
    spawns = root.findall("e:spawns/e:spawn", NS)
    result["spawn_count"] = len(spawns)

    # sound count
    sounds = root.findall("e:sounds/e:sound", NS)
    result["sound_count"] = len(sounds)
    result["has_sounds"] = len(sounds) > 0

    # transition count
    result["transitions_count"] = count_transitions(root)

    # attribution from known authors
    author = result["author"]
    if author in UPSTREAM_AUTHORS:
        result["attribution"] = UPSTREAM_AUTHORS[author]
    elif author:
        result["attribution"] = f"Author: {author} (upstream)"

    return result


def main():
    parser = argparse.ArgumentParser(
        description="Generate reproducible upstream pet package inventory"
    )
    parser.add_argument(
        "--upstream-dir",
        type=Path,
        default=Path("/tmp/for-agents/original-desktopPet"),
        help="Path to the upstream desktopPet reference checkout",
    )
    parser.add_argument(
        "--revision",
        default=None,
        help="Upstream git revision (default: read from ref checkout)",
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="Emit JSON output instead of human-readable table",
    )
    args = parser.parse_args()

    pets_dir = args.upstream_dir / "Pets"
    if not pets_dir.is_dir():
        print(
            f"error: Pets directory not found at {pets_dir}",
            file=sys.stderr,
        )
        sys.exit(1)

    ref_revision = args.revision
    if ref_revision is None:
        git_dir = args.upstream_dir / ".git"
        if git_dir.is_dir():
            import subprocess

            try:
                ref_revision = (
                    subprocess.check_output(
                        ["git", "rev-parse", "HEAD"],
                        cwd=str(args.upstream_dir),
                        stderr=subprocess.DEVNULL,
                    )
                    .decode()
                    .strip()
                )
            except Exception:
                ref_revision = "unknown"
        else:
            ref_revision = "unknown"

    packages = []
    for pet_dir in sorted(pets_dir.iterdir()):
        if not pet_dir.is_dir() or pet_dir.name.startswith("."):
            continue
        info = parse_package(pet_dir, ref_revision)
        packages.append(info)

    manifest = {
        "upstream_revision": ref_revision,
        "package_count": len(packages),
        "packages": packages,
    }

    if args.json:
        print(json.dumps(manifest, indent=2, sort_keys=True))
    else:
        print(f"Upstream revision: {ref_revision}")
        print(f"Total packages: {len(packages)}")
        print()
        print(
            f"{'Package':<20} {'Anims':>6} {'Trans':>6} {'Child':>6} "
            f"{'Spawn':>6} {'Sound':>6} {'Tiles':>9} {'HasBase64':>10}"
        )
        print("-" * 80)
        for p in packages:
            tiles = f"{p['tilesx']}x{p['tilesy']}" if p['tilesx'] else "?"
            print(
                f"{p['folder']:<20} {p['animations_count']:>6} "
                f"{p['transitions_count']:>6} {p['child_count']:>6} "
                f"{p['spawn_count']:>6} {p['sound_count']:>6} "
                f"{tiles:>9} {str(p['image_base64']):>10}"
            )
        print()
        has_sounds = [p["folder"] for p in packages if p["has_sounds"]]
        print(f"Packages with sounds ({len(has_sounds)}): {', '.join(sorted(has_sounds))}")
        unresolved = [p["folder"] for p in packages if p["license_status"] == "unresolved"]
        print(f"Packages with unresolved rights ({len(unresolved)}): {', '.join(sorted(unresolved))}")

    return manifest


if __name__ == "__main__":
    main()
