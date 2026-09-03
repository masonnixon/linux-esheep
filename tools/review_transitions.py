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


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROGRAM = os.path.join(ROOT, "esheep")


def generated_transitions(runner):
    completed = subprocess.run(
        runner + [PROGRAM, "--list-transitions"], cwd=ROOT,
        check=True, text=True, capture_output=True)
    result = []
    for line in completed.stdout.splitlines():
        fields = line.split("\t")
        if len(fields) != 7:
            continue
        index, stable_id, source, kind, context, probability, target = fields
        result.append({
            "index": int(index),
            "stable_id": int(stable_id),
            "source": int(source),
            "kind": kind,
            "context": context,
            "probability": probability,
            "target": int(target),
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

    if not os.path.exists(PROGRAM):
        subprocess.run(["make", "esheep"], cwd=ROOT, check=True)

    runner = []
    if not os.environ.get("DISPLAY") and shutil.which("xvfb-run"):
        runner = ["xvfb-run", "-a"]
    elif not os.environ.get("DISPLAY"):
        print("review_transitions: DISPLAY is unset and xvfb-run is unavailable",
              file=sys.stderr)
        return 2

    transitions = generated_transitions(runner)
    if args.index is None:
        selected = [(item["index"], item) for item in transitions]
    elif 1 <= args.index <= len(transitions):
        selected = [(args.index, transitions[args.index - 1])]
    else:
        parser.error("index must be between 1 and %d" % len(transitions))

    for index, transition in selected:
        source = transition["source"]
        target = transition["target"]
        print("transition %d/%d (id %s): animation %d --%s [%s, %s%%]--> animation %d"
              % (index, len(transitions), transition["stable_id"], source,
                 transition["kind"],
                 transition["context"], transition["probability"], target),
              flush=True)
        review_option = "--review-parent" if transition["kind"] == "child" \
            else "--review-animation"
        review_id = source if transition["kind"] == "child" else target
        command = runner + [PROGRAM, review_option, str(review_id),
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
