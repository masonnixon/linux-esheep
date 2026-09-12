#!/usr/bin/env python3
"""Tests for tools/pet_inventory.py.

Verifies:
1. Determinism: two invocations produce identical JSON output.
2. Malformed XML: parser skips or reports the package without crashing.
3. Missing source: gracefully reports the package as having errors.
4. Upstream revision: git revision is captured in the manifest.
5. Package count: all 26 upstream packages are present.
6. Known counts: sanity-check that key packages have expected counts.
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import textwrap
import xml.etree.ElementTree as ET
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
UPSTREAM_DIR = Path("/tmp/for-agents/original-desktopPet")
TOOL = REPO_ROOT / "tools" / "pet_inventory.py"
TOOL_CASES = REPO_ROOT / "tools" / "pet_inventory_cases.py"

UPSTREAM_REVISION = "48ee8022c6b0363c79213e06e4bae608a3bdc332"
UPSTREAM_PET_COUNT = 26


def run_tool(upstream_dir=None, revision=None, json_out=False):
    cmd = [sys.executable, str(TOOL)]
    if upstream_dir:
        cmd.extend(["--upstream-dir", str(upstream_dir)])
    if revision:
        cmd.extend(["--revision", revision])
    if json_out:
        cmd.append("--json")
    result = subprocess.run(
        cmd, capture_output=True, text=True, check=True
    )
    if json_out:
        return json.loads(result.stdout)
    return result.stdout


def test_tool_exists():
    assert TOOL.exists(), f"Inventory tool not found at {TOOL}"
    print("OK: tool exists")


def test_determinism():
    manifest1 = run_tool(json_out=True)
    manifest2 = run_tool(json_out=True)
    assert manifest1 == manifest2, "Two invocations produced different output"
    print("OK: output is deterministic")


def test_upstream_revision_in_manifest():
    manifest = run_tool(json_out=True)
    rev = manifest.get("upstream_revision", "")
    assert rev == UPSTREAM_REVISION, f"Expected {UPSTREAM_REVISION}, got {rev}"
    for pkg in manifest["packages"]:
        assert pkg.get("upstream_revision") == UPSTREAM_REVISION, (
            f"Package {pkg['folder']} has wrong revision"
        )
    print("OK: upstream revision captured correctly")


def test_package_count():
    manifest = run_tool(json_out=True)
    count = manifest.get("package_count", 0)
    assert count == UPSTREAM_PET_COUNT, (
        f"Expected {UPSTREAM_PET_COUNT} packages, got {count}"
    )
    assert len(manifest["packages"]) == UPSTREAM_PET_COUNT
    print(f"OK: all {UPSTREAM_PET_COUNT} packages present")


def test_all_packages_present():
    manifest = run_tool(json_out=True)
    folders = {p["folder"] for p in manifest["packages"]}
    expected = {
        "bbunny", "blue_ham_ham", "blue_sheep", "esheep64", "fox",
        "green_sheep", "grian", "mareep", "mimiko", "mumbojumbo",
        "negima", "neko", "orange_sheep", "pikachu", "pingus",
        "pink_fox", "pink_neko", "pink_sheep", "purple_sheep",
        "red_sheep", "shiny_sylveon", "skeleton", "ssj-goku",
        "yellow_neko", "yellow_sheep", "zombie",
    }
    assert folders == expected, f"Missing: {expected - folders}"
    print("OK: all expected package folders present")


def test_no_parse_errors_on_valid_upstream():
    manifest = run_tool(json_out=True)
    for pkg in manifest["packages"]:
        assert not pkg.get("errors"), (
            f"Package {pkg['folder']} had unexpected errors: {pkg['errors']}"
        )
    print("OK: no parse errors on upstream packages")


def test_known_package_counts():
    """Verify key packages match known counts from upstream."""
    manifest = run_tool(json_out=True)
    by_folder = {p["folder"]: p for p in manifest["packages"]}

    # esheep64: the bundled package we already understand
    e = by_folder["esheep64"]
    assert e["animations_count"] == 54, f"esheep64 anims: {e['animations_count']}"
    assert e["child_count"] == 3, f"esheep64 childs: {e['child_count']}"
    assert e["spawn_count"] == 4, f"esheep64 spawns: {e['spawn_count']}"
    assert e["sound_count"] == 0, f"esheep64 sounds: {e['sound_count']}"

    # bbunny: smallest package
    b = by_folder["bbunny"]
    assert b["animations_count"] == 8, f"bbunny anims: {b['animations_count']}"
    assert b["child_count"] == 0, f"bbunny childs: {b['child_count']}"

    # blue_sheep: large sheep variant
    bl = by_folder["blue_sheep"]
    assert bl["animations_count"] == 268, f"blue_sheep anims: {bl['animations_count']}"
    assert bl["sound_count"] == 35, f"blue_sheep sounds: {bl['sound_count']}"

    # fox: has sounds but fewer animations
    f = by_folder["fox"]
    assert f["animations_count"] == 27, f"fox anims: {f['animations_count']}"
    assert f["sound_count"] == 5, f"fox sounds: {f['sound_count']}"
    assert f["child_count"] == 1, f"fox childs: {f['child_count']}"

    print("OK: known package counts verified")


def test_sound_packages():
    """Verify correct packages are flagged as having sounds."""
    manifest = run_tool(json_out=True)
    has_sounds = {p["folder"] for p in manifest["packages"] if p["has_sounds"]}
    expected = {
        "blue_sheep", "fox", "green_sheep", "mimiko", "negima", "neko",
        "orange_sheep", "pink_fox", "pink_neko", "pink_sheep",
        "purple_sheep", "red_sheep", "yellow_neko", "yellow_sheep",
    }
    assert has_sounds == expected, (
        f"Mismatch in sound packages.\nExpected: {sorted(expected)}\nGot: {sorted(has_sounds)}"
    )
    print("OK: sound package flags correct")


def test_malformed_xml_directory():
    """Malformed XML should be reported as an error, not crash."""
    with tempfile.TemporaryDirectory() as tmpdir:
        bad_dir = Path(tmpdir) / "Pets" / "bad"
        bad_dir.mkdir(parents=True)
        (bad_dir / "animations.xml").write_text("<animations><bad>")
        (bad_dir / "icon.png").write_bytes(b"fake")

        manifest = run_tool(upstream_dir=Path(tmpdir), json_out=True)
        pkg = next((p for p in manifest["packages"] if p["folder"] == "bad"), None)
        assert pkg is not None, "Package should be present even with bad XML"
        assert pkg["errors"], "Malformed XML should be in errors list"
        print("OK: malformed XML handled gracefully")


def test_missing_xml_directory():
    """Directory without animations.xml should be reported as an error."""
    with tempfile.TemporaryDirectory() as tmpdir:
        empty_dir = Path(tmpdir) / "Pets" / "empty"
        empty_dir.mkdir(parents=True)
        (empty_dir / "icon.png").write_bytes(b"fake")

        manifest = run_tool(upstream_dir=Path(tmpdir), json_out=True)
        pkg = next((p for p in manifest["packages"] if p["folder"] == "empty"), None)
        assert pkg is not None, "Package should be present even without XML"
        assert pkg["errors"], "Missing XML should be in errors list"
        assert "missing animations.xml" in pkg["errors"][0]
        print("OK: missing XML handled gracefully")


def test_hashes_present():
    """Every package should have SHA-256 hashes for XML and icon."""
    manifest = run_tool(json_out=True)
    for pkg in manifest["packages"]:
        assert pkg.get("xml_sha256"), (
            f"Package {pkg['folder']} missing xml_sha256"
        )
        assert len(pkg["xml_sha256"]) == 64, (
            f"Package {pkg['folder']} has invalid SHA-256 length"
        )
        assert pkg.get("icon_sha256"), (
            f"Package {pkg['folder']} missing icon_sha256"
        )
    print("OK: all packages have SHA-256 hashes")


def test_imported_payloads():
    """The checked-in catalog must contain every manifest package payload."""
    manifest = json.loads((REPO_ROOT / "manifest.json").read_text(encoding="utf-8"))
    catalog_root = REPO_ROOT / "assets" / "Pets"
    assert manifest["package_count"] == 26
    for pkg in manifest["packages"]:
        package_dir = catalog_root / pkg["folder"]
        for name, key in (("animations.xml", "xml_sha256"),
                          ("icon.png", "icon_sha256"),
                          ("README.md", "readme_sha256")):
            artifact = package_dir / name
            assert artifact.is_file(), f"Missing imported artifact: {artifact}"
            digest = __import__("hashlib").sha256(artifact.read_bytes()).hexdigest()
            assert digest == pkg[key], f"Hash mismatch for {artifact}"
        assert pkg["license_status"] == "user-confirmed"
    print("OK: all imported catalog payloads and hashes verified")


def test_license_status_unresolved():
    """All upstream packages should be flagged as having unresolved rights."""
    manifest = run_tool(json_out=True)
    for pkg in manifest["packages"]:
        assert pkg.get("license_status") == "unresolved", (
            f"Package {pkg['folder']} should be unresolved, got {pkg.get('license_status')}"
        )
    print("OK: all packages marked with unresolved rights status")


def test_revision_argument():
    """--revision flag overrides the git revision."""
    manifest = run_tool(revision="deadbeef", json_out=True)
    assert manifest["upstream_revision"] == "deadbeef"
    for pkg in manifest["packages"]:
        assert pkg["upstream_revision"] == "deadbeef"
    print("OK: --revision flag works")


def test_missing_upstream_dir():
    """Missing upstream directory should produce a non-zero exit."""
    result = subprocess.run(
        [sys.executable, str(TOOL), "--upstream-dir", "/nonexistent"],
        capture_output=True, text=True
    )
    assert result.returncode != 0, "Should fail on missing directory"
    assert "not found" in result.stderr or "error" in result.stderr.lower()
    print("OK: missing directory handled with error")


TESTS = [
    test_tool_exists,
    test_determinism,
    test_upstream_revision_in_manifest,
    test_package_count,
    test_all_packages_present,
    test_no_parse_errors_on_valid_upstream,
    test_known_package_counts,
    test_sound_packages,
    test_malformed_xml_directory,
    test_missing_xml_directory,
    test_hashes_present,
    test_imported_payloads,
    test_license_status_unresolved,
    test_revision_argument,
    test_missing_upstream_dir,
]


if __name__ == "__main__":
    failures = 0
    for fn in TESTS:
        try:
            fn()
        except AssertionError as exc:
            failures += 1
            print(f"FAIL: {fn.__name__}: {exc}", file=sys.stderr)
        except Exception as exc:
            failures += 1
            print(f"ERROR: {fn.__name__}: {exc!r}", file=sys.stderr)
    if failures:
        print(f"\n{failures} test(s) failed.", file=sys.stderr)
        sys.exit(1)
    print(f"\nAll {len(TESTS)} inventory tests passed!")
    sys.exit(0)
