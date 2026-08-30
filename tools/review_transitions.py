#!/usr/bin/env python3
"""Play authored animation transitions one at a time for visual review.

Usage:
    tools/review_transitions.py             # play every transition
    tools/review_transitions.py 17          # play transition 17
    tools/review_transitions.py 17 3000     # show it for 3 seconds

The terminal remains the index for the currently displayed transition. The
viewer shows the target animation, which makes frame, opacity, and direction
problems easy to compare without waiting for normal random behavior.
"""
import argparse
import os
import shutil
import subprocess
import sys
import time
import xml.etree.ElementTree as ET


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
XML = os.path.join(ROOT, "tools", "esheep_animations.xml")
PROGRAM = os.path.join(ROOT, "esheep")
NS = {"e": "https://esheep.petrucci.ch/"}


def authored_transitions():
    root = ET.parse(XML).getroot()
    animations = {int(node.attrib["id"]): node
                  for node in root.find("e:animations", NS)}
    result = []
    for source_id, node in animations.items():
        for kind in ("sequence", "border", "gravity"):
            section = node.find("e:" + kind, NS)
            if section is None:
                continue
            for transition in section.findall("e:next", NS):
                result.append({
                    "source": source_id,
                    "kind": kind,
                    "context": transition.attrib.get("only", "any"),
                    "probability": transition.attrib.get("probability", "100"),
                    "target": int(transition.text.strip()),
                })

    children = root.find("e:childs", NS)
    if children is not None:
        for child in children.findall("e:child", NS):
            result.append({
                "source": int(child.attrib["animationid"]),
                "kind": "child",
                "context": "any",
                "probability": "100",
                "target": int(child.find("e:next", NS).text.strip()),
            })
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("index", nargs="?", type=int,
                        help="1-based transition index, or omit for all")
    parser.add_argument("duration", nargs="?", type=int, default=1800,
                        help="milliseconds to show each transition")
    args = parser.parse_args()
    if args.duration < 100:
        parser.error("duration must be at least 100 milliseconds")

    transitions = authored_transitions()
    if args.index is None:
        selected = list(enumerate(transitions, 1))
    elif 1 <= args.index <= len(transitions):
        selected = [(args.index, transitions[args.index - 1])]
    else:
        parser.error("index must be between 1 and %d" % len(transitions))

    if not os.path.exists(PROGRAM):
        subprocess.run(["make", "esheep"], cwd=ROOT, check=True)

    runner = []
    if not os.environ.get("DISPLAY") and shutil.which("xvfb-run"):
        runner = ["xvfb-run", "-a"]
    elif not os.environ.get("DISPLAY"):
        print("review_transitions: DISPLAY is unset and xvfb-run is unavailable",
              file=sys.stderr)
        return 2

    for index, transition in selected:
        source = transition["source"]
        target = transition["target"]
        print("transition %d/%d: animation %d --%s [%s, %s%%]--> animation %d"
              % (index, len(transitions), source, transition["kind"],
                 transition["context"], transition["probability"], target),
              flush=True)
        command = runner + [PROGRAM, "--review-animation", str(target),
                            "--spawn", "bottom", "--no-window-landing"]
        environment = os.environ.copy()
        environment["ESHEEP_AUTOQUIT_MS"] = str(args.duration)
        completed = subprocess.run(command, cwd=ROOT, env=environment)
        if completed.returncode != 0:
            return completed.returncode
        time.sleep(0.15)
    return 0


if __name__ == "__main__":
    sys.exit(main())
