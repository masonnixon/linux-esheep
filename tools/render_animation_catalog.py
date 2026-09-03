#!/usr/bin/env python3
"""Render a deterministic contact sheet of the authored animation catalog.

This is a compositor-independent visual review aid. It uses the checked-in
generated frame table and the real spritesheet, so it exposes missing frames,
unexpected blank tiles, and the authored child mapping in one artifact.
"""
import argparse
import re
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

COLS = 6
FRAMES_PER_ROW = 8
TILE_SCALE = 2
CELL_HEIGHT = 140
CELL_WIDTH = 680


def read_catalog(source):
    text = Path(source).read_text()
    frames = {}
    for name, body in re.findall(
        r"static const int (anim\d+)_frames\[\] = \{([^}]*)\};", text
    ):
        frames[int(name[4:])] = [int(value) for value in re.findall(r"\d+", body)]
    names = {}
    table = text[text.index("const EsheepAnimation esheep_default_animations[]") :]
    for animation_id, name, frame_table in re.findall(
        r'\{\s*(\d+),\s*"([^"]*)".*?(anim\d+)_frames', table, re.S
    ):
        names[int(animation_id)] = (name, frames[int(frame_table[4:])])
    children = {}
    child_table = text[text.index("const EsheepChild esheep_default_childs[]") :]
    for parent, x, y, child in re.findall(
        r"\{\s*(\d+),\s*\"([^\"]*)\",\s*\"([^\"]*)\",\s*(\d+)\s*\}",
        child_table,
    ):
        children.setdefault(int(parent), []).append((int(child), x, y))
    return names, children


def tile(sheet, index):
    size = sheet.width // 16
    left = (index % 16) * size
    top = (index // 16) * size
    return sheet.crop((left, top, left + size, top + size)).resize(
        (size * TILE_SCALE, size * TILE_SCALE), Image.Resampling.NEAREST
    )


def render(source, sprite, output):
    animations, children = read_catalog(source)
    sheet = Image.open(sprite).convert("RGBA")
    tile_size = sheet.width // 16
    scaled = tile_size * TILE_SCALE
    font = ImageFont.load_default()
    rows = (len(animations) + COLS - 1) // COLS
    catalog = Image.new("RGBA", (COLS * CELL_WIDTH, rows * CELL_HEIGHT), "#20242b")
    draw = ImageDraw.Draw(catalog)
    for position, animation_id in enumerate(sorted(animations)):
        name, frame_ids = animations[animation_id]
        cell_x = (position % COLS) * CELL_WIDTH
        cell_y = (position // COLS) * CELL_HEIGHT
        draw.text((cell_x + 6, cell_y + 5), f"{animation_id}: {name}", fill="white", font=font)
        child_text = ", ".join(str(child) for child, _, _ in children.get(animation_id, []))
        if child_text:
            draw.text((cell_x + 6, cell_y + 20), f"child: {child_text}", fill="#ffcf70", font=font)
        for frame_position, frame_id in enumerate(frame_ids[:FRAMES_PER_ROW]):
            image = tile(sheet, frame_id)
            x = cell_x + 6 + frame_position * (scaled + 2)
            y = cell_y + 38
            catalog.alpha_composite(image, (x, y))
            draw.text((x, y + scaled + 2), str(frame_id), fill="#aab4c3", font=font)
    catalog.convert("RGB").save(output, "PNG", optimize=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", default="src/animations_data.c")
    parser.add_argument("--sprite", default="assets/sheep_spritesheet.png")
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    render(args.source, args.sprite, args.output)
    print(f"Wrote animation catalog: {args.output}")


if __name__ == "__main__":
    main()
