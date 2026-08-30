#!/usr/bin/env python3
"""Build a full 16x11 (640x440) sprite sheet for the Ice Blue penguin (variant G),
laid out on the SAME tile grid as assets/sheep_spritesheet.png so it's a drop-in
replacement for the animation indices the Phase-2 engine actually needs.

Scope: only the animations reachable from `walk` using none/vertical/gravity
transitions (no window/taskbar docking yet -- that's a later phase). That's
15 animations / 36 distinct tile indices; everything else is left transparent
and documented as not-yet-drawn.
"""
from PIL import Image, ImageDraw
import sys

import os
sys.path.insert(0, os.path.dirname(__file__))
from gen_penguin_cartoon_variants import draw_cartoon_penguin as base_draw, TILE, OUTLINE

TILES_X, TILES_Y = 16, 11
SHEET_W, SHEET_H = TILES_X * TILE, TILES_Y * TILE

BODY = (30, 45, 70, 255)
BELLY = (250, 250, 248, 255)
BEAK = (245, 190, 40, 255)
FOOT = (245, 190, 40, 255)


def draw(lean=0, foot_phase=0, eyes="cross", wings="side", squash=1.0):
    img = base_draw(BODY, BELLY, BEAK, FOOT, cross_eyed=(eyes == "cross"),
                     gloss=True, lean=lean, foot_phase=foot_phase,
                     wave=(wings == "up"))
    if eyes == "closed" or squash != 1.0:
        img = img.convert("RGBA")
    if eyes == "closed":
        img = img.copy()
        d = ImageDraw.Draw(img)
        # paint over both eyes with closed-lid arcs (native res, TILE x TILE)
        for ex in (13, 27):
            d.line([(ex - 5, 13), (ex, 10), (ex + 5, 13)], fill=(20, 16, 14, 255), width=2)
            # cover the open-eye pixels underneath with belly-ish white first
        # simplest robust approach: redraw a fresh frame at higher res with closed eyes
    if squash != 1.0:
        w, h = img.size
        new_h = max(1, int(h * squash))
        img = img.resize((w, new_h), Image.NEAREST)
        canvas = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        canvas.paste(img, (0, h - new_h), img)
        img = canvas
    return img


def draw_closed_eyes(lean=0, foot_phase=0):
    """Redraw at internal resolution with closed-eye lids instead of open eyes."""
    from PIL import Image as I
    W = H = TILE * 4
    cx = W // 2 + lean * 5
    img = I.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    body_top, body_bot = int(H * 0.30), int(H * 0.95)
    body_w = int(W * 0.36)
    foot_y = body_bot - 4
    fdx = 9 * foot_phase
    d.polygon([(cx - 32 + fdx, foot_y), (cx - 4 + fdx, foot_y),
               (cx - 12 + fdx, foot_y + 20), (cx - 28 + fdx, foot_y + 20)],
              fill=FOOT, outline=OUTLINE, width=5)
    d.polygon([(cx + 4 - fdx, foot_y), (cx + 32 - fdx, foot_y),
               (cx + 28 - fdx, foot_y + 20), (cx + 12 - fdx, foot_y + 20)],
              fill=FOOT, outline=OUTLINE, width=5)
    d.ellipse([cx - body_w, body_top, cx + body_w, body_bot], fill=BODY, outline=OUTLINE, width=6)
    d.ellipse([cx + body_w - 30, body_top + 34, cx + body_w + 8, body_bot - 12], fill=BODY, outline=OUTLINE, width=5)
    d.ellipse([cx - body_w - 8, body_top + 34, cx - body_w + 30, body_bot - 12], fill=BODY, outline=OUTLINE, width=5)
    belly_w = int(body_w * 0.68)
    d.ellipse([cx - belly_w, body_top + 40, cx + belly_w, body_bot - 4], fill=BELLY, outline=None)
    head_r = int(W * 0.30)
    head_cy = body_top + int(head_r * 0.05)
    d.ellipse([cx - head_r, head_cy - head_r, cx + head_r, head_cy + head_r], fill=BODY, outline=OUTLINE, width=6)
    face_w = int(head_r * 0.86)
    face_h = int(head_r * 0.62)
    face_cy = head_cy + int(head_r * 0.08)
    d.ellipse([cx - face_w, face_cy - face_h, cx + face_w, face_cy + face_h], fill=BELLY, outline=None)
    eye_dx = int(head_r * 0.42)
    eye_cy = head_cy - int(head_r * 0.06)
    for sign in (-1, 1):
        ex = cx + sign * eye_dx
        d.arc([ex - 16, eye_cy - 12, ex + 16, eye_cy + 16], start=200, end=340, fill=OUTLINE, width=6)
    beak_w = int(head_r * 0.62)
    beak_h = int(head_r * 0.40)
    beak_cy = head_cy + int(head_r * 0.42)
    d.polygon([(cx - beak_w // 2, beak_cy - beak_h // 2),
               (cx + beak_w // 2, beak_cy - beak_h // 2),
               (cx, beak_cy + beak_h)], fill=BEAK, outline=OUTLINE)
    gw, gh = int(head_r * 0.5), int(head_r * 0.32)
    gx, gy = cx - int(head_r * 0.35), head_cy - int(head_r * 0.55)
    overlay = I.new("RGBA", img.size, (0, 0, 0, 0))
    od = ImageDraw.Draw(overlay)
    od.ellipse([gx - gw, gy - gh, gx + gw, gy + gh], fill=(255, 255, 255, 70))
    img.alpha_composite(overlay)
    return img.resize((TILE, TILE), I.NEAREST)


def draw_fall(lean=0):
    """Wings out, cross-eyed wide -- reuse base draw but with wave wings both sides."""
    img = base_draw(BODY, BELLY, BEAK, FOOT, cross_eyed=True, gloss=True,
                     lean=lean, foot_phase=0, wave=True)
    return img


sheet = Image.new("RGBA", (SHEET_W, SHEET_H), (0, 0, 0, 0))


def put(idx, img):
    tx, ty = idx % TILES_X, idx // TILES_X
    sheet.paste(img, (tx * TILE, ty * TILE), img)


# walk cycle (animation 1): frames 2, 3
put(2, draw(lean=-1, foot_phase=1))
put(3, draw(lean=1, foot_phase=-1))

# rotate/spin (animations 2, 3): frames 3 (shared with walk), 9, 10
put(9, draw(squash=0.55))          # squashed mid-roll
put(10, draw(lean=0, foot_phase=0, eyes="forward"))  # upright, dazed look

# fall (5, 6, 9, 10): frames 133, 46, 47, 48, 49
put(133, draw_fall(lean=0))
put(46, draw_fall(lean=-1))
put(47, draw_fall(lean=1))
put(48, draw_fall(lean=0))
put(49, draw(lean=0, foot_phase=0))  # landed, upright

# fall-soft landing extras: frames 12, 13
put(12, draw(squash=0.8))
put(13, draw(squash=0.65))

# sleep (15, 16): frames 0, 1, 31, 32, 33, 77, 78, 79, 80, 107, 108
put(0, draw(eyes="forward"))
put(1, draw_closed_eyes())
for i in (31, 32, 33):
    put(i, draw_closed_eyes())
for i in (77, 78, 79, 80):
    put(i, draw_closed_eyes())
for i in (107, 108):
    put(i, draw_closed_eyes())

# vertical/top-edge walking (37-42): reuse walk poses as a placeholder --
# a distinct side-view rig is future work, not drawn yet.
for i in (15, 16, 17, 19, 20, 24, 28, 30, 37, 38, 39, 97, 98):
    put(i, draw(lean=0, foot_phase=0))

out = os.path.join(os.path.dirname(__file__), "..", "assets", "penguin_ice_blue_spritesheet.png")
sheet.save(out)
print("wrote", out, sheet.size)

preview = sheet.resize((SHEET_W * 2, SHEET_H * 2), Image.NEAREST)
preview.save(os.path.join(os.path.dirname(__file__), "..", "assets", "penguin_sheet_preview.png"))