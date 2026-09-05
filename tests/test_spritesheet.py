#!/usr/bin/env python3
"""Check both sprite sheets against what src/animations_data.c actually asks for.

The first penguin sheet shipped with holes: frame indices that some animation
referenced but that were never drawn, and a tile drawn over index 174 which the
sheep sheet deliberately leaves blank. The second cut fixed those but still
left 64 cells empty that the sheep fills. All three faults are invisible until
the matching animation happens to fire, so they get asserted here instead.

The strongest check is cell-for-cell parity with the sheep sheet: the two
characters must be drop-in interchangeable, so whatever the sheep fills, the
penguin must fill too.

Run with ``make test-assets`` (or ``python3 tests/test_spritesheet.py``).
"""
import os
import re
import sys

from PIL import Image

REPO = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
COLS, ROWS = 16, 11

# Referenced by animations but intentionally empty in the sheep sheet: the
# flower and burn sequences use it as a "nothing on screen" beat.
BLANK_TILES = {174}

SHEETS = ("sheep_spritesheet.png", "penguin_ice_blue_spritesheet.png")

# Complete source-of-truth status for the 16x11 original sheet. Every filled
# cell is reachable from an authored animation; 174 is an authored blank beat
# and 173/175 are transparent padding.
ORIGINAL_REFERENCED_TILES = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 13, 15, 16, 17, 18, 19, 20,
    21, 23, 24, 25, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 58, 59, 60, 61, 62, 63,
    64, 65, 66, 67, 68, 69, 70, 76, 77, 78, 79, 80, 81, 82, 96, 97, 98,
    103, 104, 105, 106, 107, 108, 119, 127, 128, 129, 130, 133, 134, 135,
    136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 149,
    150, 151, 152, 153, 154, 155, 156, 157, 169, 170, 171, 174,
}
EXTENSION_REVIEWED_TILES = set(range(173)) - ORIGINAL_REFERENCED_TILES
ORIGINAL_TILE_STATUS = {
    index: (
        "referenced-blank" if index == 174 else
        "referenced" if index in ORIGINAL_REFERENCED_TILES else
        "transparent-padding" if index in (173, 175) else
        "extension-reachable" if index in EXTENSION_REVIEWED_TILES else
        "unused-original-art"
    )
    for index in range(COLS * ROWS)
}


def referenced_frames():
    """Every tile index reachable from esheep_animations[], from the C source."""
    src = open(os.path.join(REPO, "src", "animations_data.c")).read()
    frames = {}
    for name, body in re.findall(r"static const int (anim\d+)_frames\[\] = \{([^}]*)\};",
                                 src):
        frames[name] = [int(v) for v in re.findall(r"-?\d+", body)]

    table = src[src.index("const EsheepAnimation esheep_default_animations[]"):]
    used = set()
    for _id, _name, anim in re.findall(r'(\d+), "([^"]+)",.*?(anim\d+)_frames',
                                       table, re.S):
        used.update(frames[anim])
    return used


def tile_size(sheet):
    """The runtime derives this the same way: sheet width / tile columns."""
    return sheet.size[0] // COLS


def tile_bbox(sheet, index):
    t = tile_size(sheet)
    c, r = index % COLS, index // COLS
    # Visibility is determined by alpha, not RGB. RGB data in transparent
    # pixels must not make an otherwise empty tile appear populated.
    tile = sheet.crop((c * t, r * t, c * t + t, r * t + t))
    return tile.getchannel("A").getbbox()


def main():
    used = referenced_frames()
    failures = []

    if not used:
        failures.append("parsed no frame indices out of src/animations_data.c")

    if set(ORIGINAL_TILE_STATUS) != set(range(COLS * ROWS)):
        failures.append("original tile inventory does not cover the complete sheet")

    expected_used = set(range(173)) | BLANK_TILES
    if used != expected_used:
        failures.append(
            "animation references do not match the original tile inventory: "
            f"missing={sorted(expected_used - used)}, extra={sorted(used - expected_used)}"
        )

    for index in used:
        if index >= COLS * ROWS:
            failures.append("frame %d is outside the %dx%d grid" % (index, COLS, ROWS))

    filled = {}
    for name in SHEETS:
        path = os.path.join(REPO, "assets", name)
        sheet = Image.open(path).convert("RGBA")

        # The sheets may differ in resolution -- the runtime scales to whatever
        # sheet width / COLS gives -- but each must be a whole number of square
        # tiles on the 16x11 grid.
        t = tile_size(sheet)
        if t == 0 or sheet.size != (COLS * t, ROWS * t):
            failures.append("%s is %dx%d, not a whole %dx%d grid of square tiles"
                            % ((name,) + sheet.size + (COLS, ROWS)))
            continue

        filled[name] = {i for i in range(COLS * ROWS)
                        if tile_bbox(sheet, i) is not None}

        for index in sorted(used):
            empty = index not in filled[name]
            if index in BLANK_TILES and not empty:
                failures.append("%s: tile %d must stay blank" % (name, index))
            elif index not in BLANK_TILES and empty:
                failures.append("%s: tile %d is referenced but empty" % (name, index))

    # Cell-for-cell parity, so the two characters stay interchangeable.
    if len(filled) == len(SHEETS):
        reference, other = SHEETS[0], SHEETS[1]
        for index in sorted(filled[reference] - filled[other]):
            failures.append("%s: tile %d is empty but %s fills it"
                            % (other, index, reference))
        for index in sorted(filled[other] - filled[reference]):
            failures.append("%s: tile %d is filled but %s leaves it empty"
                            % (other, index, reference))

    if failures:
        for line in failures:
            print("FAIL: %s" % line)
        return 1

    print("All tests passed (%d referenced tiles, %d filled cells, %d sheets)"
          % (len(used), len(filled.get(SHEETS[0], ())), len(SHEETS)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
