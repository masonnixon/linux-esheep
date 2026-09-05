#!/usr/bin/env python3
"""Audit the generated transition table, with strict checks for new records."""

import os
import subprocess
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
NS = {"e": "https://esheep.petrucci.ch/"}

NEW_ANIMATIONS = {
    55: ("face_turn_extension", (11, 14)),
    56: ("hand_to_mouth_extension", (22, 26, 27)),
    57: ("glasses_reaction_extension", (52, 53, 54, 55, 56, 57)),
    58: ("hand_wave_extension", (71, 72, 73, 74, 75)),
    59: ("tumble_extension", tuple(range(83, 96))),
    60: ("tumble_recover_extension", (99, 100, 101, 102)),
    61: ("meteorite_extension", tuple(range(109, 119))),
    62: ("comet_extension", tuple(range(120, 127))),
    63: ("falling_body_extension", (131, 132)),
    64: ("atmospheric_reentry", tuple(range(133, 146))),
    65: ("spacecraft_flight", tuple(range(158, 166))),
    66: ("spacecraft_pilot", (166, 167, 168)),
    67: ("grid_overlay_extension", (172,)),
}

NEW_POSES = {
    55: 160, 56: 140, 57: 120, 58: 120, 59: 110, 60: 110,
    61: 90, 62: 90, 63: 100, 64: 70, 65: 100, 66: 100, 67: 100,
}


def authored_rows():
    root = ET.parse(ROOT / "tools/esheep_animations.xml").getroot()
    rows = []
    for animation in root.find("e:animations", NS).findall("e:animation", NS):
        source = int(animation.get("id"))
        for kind in ("sequence", "border", "gravity"):
            section = animation.find(f"e:{kind}", NS)
            if section is None:
                continue
            for nxt in section.findall("e:next", NS):
                rows.append((source, kind, nxt.get("only", "any"),
                             int(nxt.get("probability", "100")),
                             int((nxt.text or "").strip())))
    for child in root.find("e:childs", NS).findall("e:child", NS):
        rows.append((int(child.get("animationid")), "child", "any", 100,
                     int(child.findtext("e:next", namespaces=NS))))
    return rows


def exported_rows():
    env = os.environ.copy()
    completed = subprocess.run([str(ROOT / "esheep"), "--list-transitions"],
                               cwd=ROOT, check=True, capture_output=True,
                               text=True, env=env)
    rows = []
    stable_ids = set()
    for line in completed.stdout.splitlines():
        fields = line.split("\t")
        if len(fields) != 7:
            continue
        index, stable_id, source, kind, context, probability, target = fields
        stable_id = int(stable_id)
        assert stable_id not in stable_ids, f"duplicate stable ID at {index}"
        stable_ids.add(stable_id)
        rows.append((int(index), int(source), kind, context,
                     int(probability), int(target)))
    return rows


def test_new_animation_definitions():
    root = ET.parse(ROOT / "tools/esheep_animations.xml").getroot()
    animations = root.find("e:animations", NS)
    for animation_id, (name, frames) in NEW_ANIMATIONS.items():
        animation = animations.find(f"e:animation[@id='{animation_id}']", NS)
        assert animation is not None
        assert animation.findtext("e:name", namespaces=NS) == name
        actual = tuple(int(frame.text) for frame in
                       animation.findall("e:sequence/e:frame", NS))
        assert actual == frames, f"animation {animation_id} frame sequence changed"
        for pose_name in ("start", "end"):
            pose = animation.find(f"e:{pose_name}", NS)
            assert (pose.findtext("e:x", namespaces=NS),
                    pose.findtext("e:y", namespaces=NS),
                    pose.findtext("e:interval", namespaces=NS)) == (
                        "0", "0", str(NEW_POSES[animation_id]))
        sequence = animation.find("e:sequence", NS)
        assert (sequence.get("repeat"), sequence.get("repeatfrom")) == ("0", "0")
        assert sequence.find("e:action", NS) is None
        nxt = sequence.find("e:next", NS)
        assert (nxt.get("probability"), nxt.get("only"),
                int(nxt.text.strip())) == ("100", "none", 1)


def test_new_child_placement():
    root = ET.parse(ROOT / "tools/esheep_animations.xml").getroot()
    child = root.find("e:childs/e:child[@animationid='65']", NS)
    assert child is not None
    assert (child.findtext("e:x", namespaces=NS),
            child.findtext("e:y", namespaces=NS),
            int(child.findtext("e:next", namespaces=NS))) == ("0", "imageH", 66)


def main():
    authored = authored_rows()
    exported = exported_rows()
    assert len(authored) == 110, f"expected 110 authored transitions, got {len(authored)}"
    assert len(exported) == 110, f"expected 110 exported transitions, got {len(exported)}"

    for expected, actual in zip(authored, exported):
        index, source, kind, context, probability, target = actual
        assert index == len([row for row in exported if row[0] <= index])
        assert actual[1:] == expected, (
            f"transition {index} mismatch: exported={actual[1:]}, authored={expected}"
        )
        assert 1 <= source <= 67 and 1 <= target <= 67
        assert 1 <= probability <= 100

    test_new_animation_definitions()
    test_new_child_placement()
    new_rows = [row for row in exported if row[1] >= 55]
    assert len(new_rows) == 14, f"expected 14 new transition rows, got {len(new_rows)}"
    assert exported[93:106] == [
        (index, animation_id, "sequence", "none", 100, 1)
        for index, animation_id in zip(range(94, 107), range(55, 68))
    ]
    assert exported[106:110] == [
        (107, 21, "child", "any", 100, 23),
        (108, 26, "child", "any", 100, 27),
        (109, 28, "child", "any", 100, 31),
        (110, 65, "child", "any", 100, 66),
    ]
    assert new_rows[-1][1:] == (65, "child", "any", 100, 66)
    print("All 110 transitions match authored data; new transitions 94-110 verified")


if __name__ == "__main__":
    main()
