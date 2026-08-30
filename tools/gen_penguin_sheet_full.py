#!/usr/bin/env python3
"""Fill in every tile the full 54-animation eSheep graph actually uses (110 of
176 grid cells) for the Ice Blue penguin. Reuses the Phase-2 MVP poses already
placed (walk/fall/spin/sleep/climb, 36 tiles) and adds the rest: run, jump,
grooming, dizzy/reaction shots, bounce squash-stretch, catching fire, sooty
aftermath, and a black-morph gag. Only the flower-growth tiles (149-153) are
true scene props copied directly from the sheep sheet; everything else,
including the closeup reaction shots, is redrawn as the penguin -- do not
assume a tile is a generic prop just because it looks decorative without
checking the actual sheep-sheet art first (169-171 look like they could be
a water trough at a glance; they are not).
"""
import os
import sys
from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_penguin_cartoon_variants import draw_cartoon_penguin as base_draw, TILE, OUTLINE
from gen_penguin_sheet import draw_closed_eyes, BODY, BELLY, BEAK, FOOT, draw as draw_walk_family, draw_fall

TILES_X, TILES_Y = 16, 11
SHEET_W, SHEET_H = TILES_X * TILE, TILES_Y * TILE
REPO = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")

BLACK_BODY = (18, 18, 20, 255)


def add_cheeks(img, strength=180):
    img = img.copy()
    d = ImageDraw.Draw(img)
    for sign in (-1, 1):
        cx = TILE // 2 + sign * 9
        cy = 15
        d.ellipse([cx - 3, cy - 2, cx + 3, cy + 2], fill=(255, 150, 150, strength))
    return img


def add_flames(img, level):
    """level 1..3, jagged flame shapes climbing the body silhouette."""
    img = img.copy()
    d = ImageDraw.Draw(img)
    colors = [(255, 140, 30, 235), (255, 90, 20, 210), (255, 210, 60, 190)]
    h = 6 + level * 5
    import random
    rnd = random.Random(level * 97 + 3)
    for i in range(3 + level * 2):
        bx = 4 + rnd.randint(0, TILE - 8)
        by = TILE - rnd.randint(4, 14)
        w = rnd.randint(3, 6)
        peak = by - h - rnd.randint(0, 6)
        c = colors[i % len(colors)]
        d.polygon([(bx - w, by), (bx, peak), (bx + w, by)], fill=c)
    return img


def add_soot(img, level):
    img = img.copy()
    d = ImageDraw.Draw(img)
    import random
    rnd = random.Random(level * 53 + 11)
    for _ in range(10 + level * 8):
        x = rnd.randint(4, TILE - 4)
        y = rnd.randint(6, TILE - 6)
        r = rnd.randint(1, 2)
        d.ellipse([x - r, y - r, x + r, y + r], fill=(25, 22, 20, 220))
    return img


def add_half_lids(img):
    """Droopy half-closed eyelids (a lowered lid line, not a full blackout),
    for a blink/drowsy sequence -- pupil still peeks out underneath."""
    img = img.copy()
    d = ImageDraw.Draw(img)
    for ex in (13, 27):
        ey = 15
        d.chord([ex - 7, ey - 7, ex + 7, ey + 7], start=190, end=350,
                fill=BELLY, outline=OUTLINE, width=2)
    return img


def make_dizzy(img):
    img = img.copy()
    d = ImageDraw.Draw(img)
    for ex in (13, 27):
        ey = 15
        d.ellipse([ex - 6, ey - 6, ex + 6, ey + 6], fill=(255, 255, 255, 255), outline=OUTLINE, width=2)
        d.line([(ex - 4, ey - 4), (ex + 4, ey + 4)], fill=(20, 16, 14, 255), width=2)
        d.line([(ex - 4, ey + 4), (ex + 4, ey - 4)], fill=(20, 16, 14, 255), width=2)
    return img


def zoom_closeup(img, cx_frac=0.5, cy_frac=0.35, zoom=2.2):
    size = TILE / zoom
    cx, cy = cx_frac * TILE, cy_frac * TILE
    x0, y0 = int(cx - size / 2), int(cy - size / 2)
    x1, y1 = int(cx + size / 2), int(cy + size / 2)
    # crop manually onto a transparent canvas -- Image.crop() pads out-of-bounds
    # area with opaque black for RGBA on some Pillow builds, not transparent.
    cw, ch = x1 - x0, y1 - y0
    canvas = Image.new("RGBA", (cw, ch), (0, 0, 0, 0))
    sx0, sy0 = max(0, x0), max(0, y0)
    sx1, sy1 = min(TILE, x1), min(TILE, y1)
    if sx1 > sx0 and sy1 > sy0:
        region = img.crop((sx0, sy0, sx1, sy1))
        canvas.paste(region, (sx0 - x0, sy0 - y0), region)
    return canvas.resize((TILE, TILE), Image.NEAREST)


def stand(body=BODY, lean=0, foot_phase=0, cross=True, wave=False):
    return base_draw(body, BELLY, BEAK, FOOT, cross_eyed=cross, gloss=True,
                      lean=lean, foot_phase=foot_phase, wave=wave)


def squash_stretch(factor):
    img = stand(lean=0, foot_phase=0, cross=False)
    w, h = img.size
    new_h = max(1, int(h * factor))
    scaled = img.resize((w, new_h), Image.NEAREST)
    canvas = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    if new_h <= h:
        canvas.paste(scaled, (0, h - new_h), scaled)
    else:
        canvas.paste(scaled, (0, h - new_h), scaled)  # allow slight overflow crop at top
        canvas = canvas.crop((0, new_h - h, w, new_h)).resize((w, h)) if False else canvas
    return canvas


sheet = Image.open(os.path.join(REPO, "assets", "penguin_ice_blue_spritesheet.png")).convert("RGBA")


def put(idx, img):
    tx, ty = idx % TILES_X, idx // TILES_X
    sheet.paste(Image.new("RGBA", (TILE, TILE), (0, 0, 0, 0)), (tx * TILE, ty * TILE))
    sheet.paste(img, (tx * TILE, ty * TILE), img)


# --- tile 6: settled/calm filler frame, missed in the Phase-2 MVP pass
# (used as a resting beat in fall-soft, sleep1b, and vertical_walk_over) ---
put(6, draw_closed_eyes())

# --- true scene props: copy straight from the sheep sheet, unchanged ---
sheep_sheet = Image.open(os.path.join(REPO, "assets", "sheep_spritesheet.png")).convert("RGBA")


def copy_prop(idx):
    tx, ty = idx % TILES_X, idx // TILES_X
    tile = sheep_sheet.crop((tx * TILE, ty * TILE, tx * TILE + TILE, ty * TILE + TILE))
    put(idx, tile)


for idx in (149, 150, 151, 152, 153):
    copy_prop(idx)

# --- run / stride (4, 5) ---
put(4, stand(lean=1, foot_phase=2, cross=False))
put(5, stand(lean=-1, foot_phase=-2, cross=False))

# --- grooming / closeup looks (7, 8) ---
put(7, zoom_closeup(stand(cross=True), cx_frac=0.42))
put(8, zoom_closeup(stand(cross=True, wave=True), cx_frac=0.58))

# --- alert / look-up (18, 21, 29) ---
put(18, stand(cross=False, wave=True))
put(21, stand(cross=False))
put(29, stand(lean=-1, foot_phase=1, cross=False))

# --- closeup surprised single-eye reactions (23, 25, 50, 51, 68, 69) ---
for idx, cxf in ((23, 0.4), (25, 0.6), (50, 0.4), (51, 0.6), (68, 0.42), (69, 0.58)):
    put(idx, zoom_closeup(stand(cross=False), cx_frac=cxf))

# --- sleepy half-lid (34, 35, 36) ---
for idx in (34, 35, 36):
    put(idx, draw_closed_eyes())

# --- grooming raised-wing (40, 41) ---
put(40, stand(wave=True, cross=False))
put(41, stand(wave=True, cross=True, lean=1))

# --- covering-face / startled (42, 43, 44, 45) ---
put(42, stand(wave=True, lean=-1))
put(43, stand(wave=True, lean=1))
put(44, stand(wave=True))
put(45, add_cheeks(stand(cross=False)))

# --- eat / munching (58, 59, 60, 61) ---
for idx, ph in ((58, 0), (59, 1), (60, -1), (61, 0)):
    put(idx, stand(lean=0, foot_phase=ph, cross=True))

# --- boing bounce squash/stretch (62-67, 70) ---
for idx, f in ((62, 0.55), (63, 0.7), (64, 1.15), (65, 0.85), (66, 1.1), (67, 0.75), (70, 1.0)):
    put(idx, squash_stretch(f))

# --- jump arch (76) ---
put(76, stand(wave=True, lean=1, cross=False))

# --- dizzy reactions (96, 127, 128, 129, 130) ---
put(96, zoom_closeup(make_dizzy(stand(cross=False)), cx_frac=0.5))
for idx, lean in ((127, -1), (128, 1), (129, -1), (130, 1)):
    put(idx, make_dizzy(stand(lean=lean, cross=False)))

# --- closeup blink sequence (animations 47 "bathc" and 48 "bathd" cycle
# through these -- open eyes, drowsy half-lids, fully closed -- as a
# closeup reaction shot, NOT a water trough; I originally mis-copied 169-171
# straight from the sheep sheet here, which is exactly wrong: those three
# ARE sheep character closeups in the source, so a literal copy puts actual
# sheep art in the penguin sheet. Redrawn as penguin closeups instead. ---
_blink_open = zoom_closeup(stand(cross=False), cx_frac=0.5, cy_frac=0.32, zoom=1.8)
_blink_half = zoom_closeup(add_half_lids(stand(cross=False)), cx_frac=0.5, cy_frac=0.32, zoom=1.8)
_blink_closed = zoom_closeup(draw_closed_eyes(), cx_frac=0.5, cy_frac=0.32, zoom=1.8)
put(119, _blink_open)
put(81, _blink_half)
put(82, _blink_closed)
put(169, _blink_open)
put(170, _blink_half)
put(171, _blink_closed)

# --- crouch gag (103, 104, 105, 106) ---
for idx, lean in ((103, 0), (104, -1), (105, 1), (106, 0)):
    put(idx, add_cheeks(stand(lean=lean, foot_phase=1, cross=True), strength=130))

# --- bath idle (134) ---
put(134, stand(cross=False))

# --- catching fire, low to high (135-140) ---
for i, idx in enumerate((135, 136, 137, 138, 139, 140)):
    level = 1 + i // 2
    put(idx, add_flames(stand(cross=False, wave=(i % 2 == 1)), level))

# --- fire + panic blush (141-145) ---
for idx in (141, 142, 143, 144, 145):
    put(idx, add_cheeks(add_flames(stand(cross=False), 3), strength=200))

# --- soot aftermath, fire dying (146, 147, 148) ---
put(146, add_soot(add_flames(stand(cross=False), 2), 1))
put(147, add_soot(add_flames(stand(cross=False), 1), 2))
put(148, add_soot(stand(cross=False), 3))

# --- content closeup face, bookends the bath/flower animations (174) ---
put(174, zoom_closeup(stand(cross=False), cx_frac=0.5, cy_frac=0.32, zoom=1.7))

# --- black-morph gag (154-157) ---
for idx, lean in ((154, 0), (155, -1), (156, 1), (157, 0)):
    put(idx, stand(body=BLACK_BODY, lean=lean, cross=False))

out = os.path.join(REPO, "assets", "penguin_ice_blue_spritesheet.png")
sheet.save(out)
print("wrote", out, sheet.size)

preview = sheet.resize((SHEET_W * 2, SHEET_H * 2), Image.NEAREST)
preview_path = os.path.join(REPO, "assets", "penguin_sheet_preview.png")
preview.save(preview_path)
print("wrote", preview_path)
