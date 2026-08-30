#!/usr/bin/env python3
"""Build assets/penguin_ice_blue_spritesheet.png from tools/penguin_rig.py.

The sheet is a 16x11 grid of 40px tiles (640x440), the same geometry as
assets/sheep_spritesheet.png, so the frame indices in ``esheep_animations[]``
resolve identically for either character.

Only the 110 tiles that some animation actually references are drawn; the other
66 grid cells are unreferenced in the sheep data too and stay transparent. Tile
174 is referenced but is *deliberately* blank -- the sheep sheet leaves it empty
and the flower/burn animations use it as a "nothing on screen" beat.

Run from anywhere: ``python3 tools/gen_penguin_spritesheet.py``. It is
idempotent and rebuilds the whole sheet from scratch.
"""
import math
import os
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import penguin_rig as R  # noqa: E402

REPO = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
TILE = R.TILE            # output pixels per tile
UNITS = R.UNITS          # authoring coordinate space
SHEEP_TILE = 40          # the sheep sheet's own tile size, for copied scenery
COLS, ROWS = 16, 11


def u(value):
    """Authoring units -> output pixels."""
    return int(round(value * TILE / UNITS))


def from_sheep(index):
    """Lift a scenery tile out of the sheep sheet at this sheet's resolution."""
    c, r = index % COLS, index // COLS
    tile = _sheep.crop((c * SHEEP_TILE, r * SHEEP_TILE,
                        c * SHEEP_TILE + SHEEP_TILE, r * SHEEP_TILE + SHEEP_TILE))
    return tile if TILE == SHEEP_TILE else tile.resize((TILE, TILE), Image.NEAREST)

tiles = {}


def put(index, img):
    """Register a finished tile. Accepts a supersampled canvas or a 40x40 tile."""
    if img.size != (TILE, TILE):
        img = R.finish(img)
    tiles[index] = img


def overlay(base, fn):
    """Draw extra art onto a supersampled canvas."""
    fn(ImageDraw.Draw(base), base)
    return base


def rot(index_or_img, deg):
    """Rotate a finished 40x40 tile (counter-clockwise), keeping it 40x40."""
    img = index_or_img if isinstance(index_or_img, Image.Image) else tiles[index_or_img]
    return img.rotate(deg, resample=Image.NEAREST, expand=False)


def mirror(index_or_img):
    """Horizontal flip of a finished 40x40 tile."""
    img = (index_or_img if isinstance(index_or_img, Image.Image)
           else tiles[index_or_img])
    return img.transpose(Image.FLIP_LEFT_RIGHT)


def shrink(img, size):
    """Centre a scaled-down copy of a tile, so rotating it will not clip."""
    small = img.resize((size, size), Image.NEAREST)
    out = Image.new("RGBA", (TILE, TILE), (0, 0, 0, 0))
    out.paste(small, ((TILE - size) // 2, (TILE - size) // 2))
    return out


# =========================================================== locomotion =====

put(2, R.side(foot_phase=0.85, lean=-2.0, near_ang=20.0, far_ang=-18.0))
put(3, R.side(foot_phase=-0.85, lean=1.0, near_ang=-6.0, far_ang=14.0))
put(6, R.side())
put(4, R.side(foot_phase=0.95, lean=-9.0, crouch=1.2, near_ang=-54.0, far_ang=-42.0))
put(5, R.side(foot_phase=-0.95, lean=-6.0, crouch=0.6, near_ang=48.0, far_ang=36.0))

# turn-around: profile -> three-quarter -> full front
put(9, R.three_quarter())
put(10, R.front())

# =============================================== idle, drowsy and sleeping ==

put(31, R.side(head_dy=-0.9, look=(0.0, -0.5)))
put(32, R.side(eye_state="closed"))
put(33, R.side(eye_state="closed", head_dy=1.6, crouch=0.9))
put(7, R.side(eye_state="half"))
put(8, R.side(eye_state="closed", head_dy=1.8, head_dx=-0.4, crouch=1.2,
              head_ang=-14.0))
put(0, R.side(eye_state="closed", head_dy=2.4, head_dx=-0.8, crouch=1.8,
              head_ang=-24.0))
put(1, R.side(eye_state="closed", head_dy=2.8, head_dx=-1.2, crouch=2.2,
              head_ang=-32.0))


# ============================================== peering down over a ledge ===

put(37, R.side(look=(0.0, 0.7)))
put(107, R.side(look=(0.0, 0.8), head_dy=1.6, head_ang=-18.0))
put(108, R.side(eye_state="half", head_dy=1.6, head_ang=-18.0))
put(38, R.side(look=(0.0, 0.9), head_dy=2.4, head_dx=-1.0, head_ang=-38.0))
put(39, R.side(eye_state="closed", head_dy=2.4, head_dx=-1.0, head_ang=-38.0))
put(77, R.side(look=(0.0, 1.0), head_dy=2.8, head_dx=-1.2, head_ang=-56.0,
               crouch=0.8))
put(78, R.side(look=(0.0, 1.0), head_dy=3.2, head_dx=-1.4, head_ang=-68.0,
               crouch=1.2))
put(79, R.side(eye_state="half", head_dy=3.2, head_dx=-1.4, head_ang=-68.0,
               crouch=1.2))
put(80, R.side(look=(-0.4, 1.0), head_dy=3.0, head_dx=-1.3, head_ang=-62.0,
               crouch=1.0))

# ================================================================ eating ====

put(58, R.side(eye_state="half", head_dy=3.2, head_dx=-1.4, head_ang=-66.0,
               crouch=1.4))
put(59, R.side(eye_state="half", head_dy=3.2, head_dx=-1.4, head_ang=-66.0,
               crouch=1.4, beak_open=1.0))
put(60, R.side(eye_state="half", head_dy=0.4, beak_open=0.95))
put(61, R.side(eye_state="half", head_dy=0.4, beak_open=0.15))

# ============================================================= reactions ====

put(50, R.side(eye_state="spiral"))
put(51, R.side(eye_state="spiral", head_dy=0.9, lean=2.0, near_ang=-24.0))
put(76, R.side(look=(-0.6, -0.6), crouch=2.6, squash=0.86, near_ang=-46.0))
put(46, R.front(arms_up=True, look=(0.0, -0.6)))
put(47, R.front(eye_state="x"))
put(48, R.front(eye_state="spiral"))


def _sweat_at(x, y, size=1.0):
    def fn(d, _img):
        R.sweat(d, x, y, size)
    return fn


put(134, overlay(R.side(eye_state="spiral", crouch=0.6), _sweat_at(30.0, 9.0, 1.3)))
put(133, overlay(R.back(), _sweat_at(20.0, 9.5, 1.5)))


def _bleed(stage):
    """Beak-open retching frames used by the black-morph gag."""
    img = R.side(eye_state="x" if stage > 1 else "half", head_dy=1.4,
                 head_ang=-24.0, beak_open=0.4 + 0.2 * stage, crouch=0.8)
    d = ImageDraw.Draw(img)
    n = 2 + stage
    for i in range(n):
        t = i / float(n)
        R.ell(d, 3.4 - t * 3.0, 20.0 + t * 12.0, 1.5 - t * 0.6, 1.5 - t * 0.6,
              fill=R.RED)
    return img


for i, stage in zip((127, 128, 129, 130), (0, 1, 2, 3)):
    put(i, _bleed(stage))


def _dead():
    """Flat on its back, feet up, X eyes -- the 'kill' pose."""
    img = R.canvas()
    d = ImageDraw.Draw(img)
    ground = 38.6
    bcx, bcy = 21.0, ground - 7.0
    R.ell(d, bcx, bcy, 14.6, 7.0, fill=R.BODY, outline=R.OUTLINE, width=0.75)
    R.ell(d, bcx - 3.0, bcy + 1.2, 10.2, 4.6, fill=R.BELLY)
    R._flipper(d, bcx + 5.0, bcy - 4.0, 24.0, 0.8, R.BODY, R.OUTLINE)
    R._foot_side(d, bcx - 3.0, bcy - 6.0, -1, ang=-150)
    R._foot_side(d, bcx + 3.4, bcy - 6.4, 1, ang=150)
    hx, hy = 7.4, bcy - 1.0
    R.ell(d, hx, hy, 8.0, 8.0, fill=R.BODY, outline=R.OUTLINE, width=0.75)
    R.ell(d, hx - 1.2, hy + 1.4, 5.2, 4.2, fill=R.BELLY)
    R._beak_side(d, hx - 7.6, hy + 2.4, 0.3, 0.85, ang=18.0)
    R.eye(d, hx - 1.6, hy - 1.2, 3.4, "x")
    R.shade(img, bcx, bcy, 14.6, 7.0)
    return img


put(96, _dead())

# ============================================================== rear view ===

put(12, R.back(foot_phase=0.45))
put(13, R.back(foot_phase=-0.45))
put(49, R.back(squash=0.94))
put(103, R.back(foot_phase=0.65))
put(104, R.back(foot_phase=-0.65))


def _relief(length):
    img = R.back(foot_phase=-0.5)
    d = ImageDraw.Draw(img)
    for i in range(length):
        t = i / float(length)
        R.ell(d, 12.0 - t * 8.0, 30.0 + t * 8.0, 1.2, 1.2, fill=R.WATER)
    return img


put(105, _relief(4))
put(106, _relief(7))

# dragged by the mouse: hanging by the scruff, feet kicking
put(42, R.back(legs_up=True, wings_out=True, tail_side=-0.8))
put(43, R.back(legs_up=True, wings_out=True, tail_side=0.8))
put(44, R.back(legs_up=True, wings_out=True, tail_side=0.0, squash=0.96))

# ================================================= walls, ceilings, falls ===
# The engine never rotates a tile at runtime, so the climbing and ceiling
# frames are authored pre-rotated -- exactly how the sheep sheet does it.

put(15, rot(2, 270))
put(16, rot(3, 270))
put(17, rot(6, 270))
put(18, rot(5, 270))

put(19, rot(2, 90))
put(20, rot(3, 90))
put(21, rot(4, 90))
put(25, rot(4, 90))

# hauling itself over a top edge
put(23, R.side(look=(-0.5, -0.8), lean=-11.0, crouch=2.2, wing_up=True))
put(28, R.side(look=(-0.3, -0.9), lean=-6.0, crouch=1.2, near_ang=-64.0))
put(30, R.side(look=(0.0, -0.7), lean=-3.0, near_ang=-40.0))
put(24, rot(30, 90))

# scrabbling down a window edge
_scrabble = R.side(look=(-0.6, 0.4), lean=-7.0, crouch=1.6, near_ang=-70.0,
                   far_ang=62.0)
put(29, rot(R.finish(_scrabble), 270))
put(40, rot(R.finish(R.side(look=(-0.6, 0.5), lean=-5.0, crouch=1.2,
                            near_ang=-58.0, far_ang=50.0)), 270))
put(41, rot(R.finish(R.side(look=(-0.6, 0.6), lean=-9.0, crouch=2.0,
                            near_ang=-78.0, far_ang=70.0)), 270))
put(45, rot(R.finish(R.side(eye_state="spiral", lean=-9.0, crouch=2.0,
                            near_ang=-78.0, far_ang=70.0)), 90))

# upside down along a ceiling
put(97, rot(2, 180))
put(98, rot(3, 180))

# ================================================================= boing ====
# A tumbling bounce: one tucked ball, spun through eight 45-degree steps, with
# a squashed impact frame to start it off.

def _ball(squash=1.0, eye_state="spiral"):
    """Rolled into a ball: near-circular, so it survives being rotated."""
    img = R.canvas()
    d = ImageDraw.Draw(img)
    cx, cy = 20.0, 20.0
    rx, ry = 16.4, 16.4 * squash
    # feet and flippers tucked against the body
    R._foot_side(d, cx - 8.0, cy + ry * 0.86, -1, ang=-26.0)
    R._foot_side(d, cx + 2.0, cy + ry * 0.92, -1, ang=-14.0)
    R.ell(d, cx, cy, rx, ry, fill=R.BODY, outline=R.OUTLINE, width=0.75)
    R.ell(d, cx - rx * 0.20, cy + ry * 0.26, rx * 0.62, ry * 0.56, fill=R.BELLY)
    R._flipper(d, cx + rx * 0.44, cy + ry * 0.10, -34.0, 0.80, R.BODY, R.OUTLINE)
    # head folded down into the chest
    hx, hy, hr = cx - rx * 0.44, cy - ry * 0.34, 8.6
    R.ell(d, hx, hy, hr, hr, fill=R.BODY, outline=R.OUTLINE, width=0.7)
    R.ell(d, hx - hr * 0.24, hy + hr * 0.34, hr * 0.62, hr * 0.48, fill=R.BELLY)
    R._beak_side(d, hx - hr * 0.86, hy + hr * 0.52, 0.0, 0.8, ang=34.0)
    R.eye(d, hx - hr * 0.18, hy - hr * 0.12, 3.6, eye_state)
    R._gloss(img, hx - hr * 0.26, hy - hr * 0.54, hr * 0.40, hr * 0.24,
             clip=(hx, hy, hr - 0.9))
    R.shade(img, cx, cy, rx, ry)
    return img


def _impact():
    """First frame of the bounce: flattened against the ground."""
    img = _ball(squash=0.58)
    d = ImageDraw.Draw(img)
    for dy in (-4.0, 0.0, 4.0):
        R.line(d, [(36.4, 20.0 + dy), (39.4, 20.0 + dy)], R.OUTLINE, 0.7)
        R.line(d, [(0.6, 20.0 + dy), (3.6, 20.0 + dy)], R.OUTLINE, 0.7)
    return img


put(62, _impact())
_spin = R.finish(_ball())
for n, index in enumerate((63, 64, 65, 66, 67, 68, 69, 70)):
    put(index, rot(_spin, (n + 1) * 45))

# ========================================================= face closeups ====
# A 40x18 band across the top of the tile -- the sync/bath reaction shots.


def _closeup(eye_state, blush=False):
    img = R.canvas()
    d = ImageDraw.Draw(img)
    R.ell(d, 20.0, 6.0, 20.4, 12.4, fill=R.BODY, outline=R.OUTLINE, width=0.8)
    R.ell(d, 20.0, 8.0, 14.4, 8.4, fill=R.BELLY)
    for sign in (-1, 1):
        R.eye(d, 20.0 + sign * 6.8, 6.2, 4.6, eye_state)
    R.poly(d, [(16.6, 11.4), (23.4, 11.4), (20.0, 16.4)],
           fill=R.BEAK, outline=R.OUTLINE, width=0.5)
    if blush:
        for sign in (-1, 1):
            R.ell(d, 20.0 + sign * 13.6, 10.4, 3.0, 1.6, fill=R.BLUSH)
    out = R.finish(img)
    band = Image.new("RGBA", (TILE, TILE), (0, 0, 0, 0))
    band.paste(out.crop((0, 0, TILE, u(18))), (0, 0))
    return band


put(169, _closeup("open"))
put(170, _closeup("half"))
put(171, _closeup("closed", blush=True))

# =============================================== catching fire, then soot ====
# bathb runs 135..145 then holds on the blank tile 174.

_BURN = [
    # (flame intensity, soot amount, eye state)
    (0.45, 0, "open"), (0.65, 0, "open"), (0.85, 0, "x"), (1.00, 0, "x"),
    (1.10, 20, "x"), (1.10, 45, "x"), (1.05, 75, "x"), (0.95, 105, "x"),
    (0.70, 135, "x"), (0.40, 160, "x"), (0.15, 180, "x"),
]

for n, (heat, ash, es) in enumerate(_BURN):
    index = 135 + n
    body = R.BODY if ash == 0 else R.SOOT
    belly = R.BELLY if ash == 0 else R.SOOT_LT
    body_img = R.side(eye_state=es, body_color=body, belly_color=belly,
                      beak_color=R.BEAK if ash < 100 else R.BEAK_DK,
                      crouch=0.4 + n * 0.12, lean=(-1) ** n * 2.0,
                      gloss=ash == 0)
    if ash:
        R.soot(ImageDraw.Draw(body_img), 21.4, 24.0, 12.0, 11.0,
               density=ash, seed=n + 1)
    if heat > 0.05:
        img = R.flame_layer(21.4, 22.4, 13.8, 12.8, heat * 1.35, seed=n + 1)
        img.alpha_composite(body_img)
        R.flame_tongues(ImageDraw.Draw(img), 21.4, 22.4, 13.8, 12.8, heat,
                        seed=n + 1)
    else:
        img = body_img
    put(index, img)

# ============================================== the black-penguin cameo =====
# Opaque black tile, silhouette walking the other way -- same gag as the sheep.


def _black(foot_phase, lean):
    img = R.side(foot_phase=foot_phase, lean=lean, body_color=(26, 26, 28, 255),
                 belly_color=(126, 126, 124, 255), beak_color=(96, 94, 90, 255),
                 outline=(4, 4, 6, 255), gloss=False)
    out = R.finish(img).transpose(Image.FLIP_LEFT_RIGHT)
    bg = Image.new("RGBA", (TILE, TILE), (0, 0, 0, 255))
    bg.alpha_composite(out)
    return bg


put(154, _black(0.85, -2.0))
put(155, _black(0.0, 0.0))
put(156, _black(-0.85, 1.0))
put(157, _black(0.4, -1.0))

# ================================================================= bath =====


def _tub(splash):
    """Ice-blue tub with a shower head; ``splash`` in {0, 1, 2}."""
    img = R.canvas()
    d = ImageDraw.Draw(img)
    # shower pipe and head, rising out of the top-left corner
    R.poly(d, [(3.0, -1.0), (6.6, -1.0), (6.6, 6.0), (3.0, 6.0)],
           fill=(158, 166, 176, 255), outline=R.OUTLINE, width=0.55)
    R.poly(d, [(3.0, 2.6), (13.0, 2.6), (13.0, 6.0), (3.0, 6.0)],
           fill=(178, 186, 196, 255), outline=R.OUTLINE, width=0.55)
    R.poly(d, [(9.2, 5.4), (15.0, 5.4), (13.6, 10.4), (10.6, 10.4)],
           fill=R.GLASS, outline=R.OUTLINE, width=0.6)
    if splash:
        n = 6 if splash == 1 else 11
        spread = 9.0 if splash == 1 else 20.0
        for i in range(n):
            t = (i + 0.5) / n
            x = 10.8 + t * spread
            h = (6.0 + 5.0 * splash) * (0.55 + 0.45 * math.sin(t * math.pi))
            R.line(d, [(x, 11.0), (x + 2.4, 11.0 + h)], R.WATER, 1.0)
    # tub
    R.poly(d, [(0.5, 21.0), (39.5, 21.0), (39.5, 25.5), (0.5, 25.5)],
           fill=(232, 242, 250, 255), outline=R.OUTLINE, width=0.7)
    R.poly(d, [(1.4, 25.5), (38.6, 25.5), (34.8, 37.0), (5.2, 37.0)],
           fill=(186, 212, 232, 255), outline=R.OUTLINE, width=0.7)
    R.line(d, [(8.0, 28.5), (15.0, 33.5)], (244, 251, 255, 255), 1.1)
    R.line(d, [(19.0, 29.0), (24.0, 32.5)], (244, 251, 255, 255), 1.1)
    for fx in (7.5, 32.5):
        R.poly(d, [(fx - 2.0, 37.0), (fx + 2.0, 37.0), (fx + 1.4, 39.5),
                   (fx - 1.4, 39.5)], fill=R.OUTLINE)
    return img


put(146, _tub(0))
put(147, _tub(1))
put(148, _tub(2))

# =============================================== flowers (scene props) ======
# Character-neutral plant art; taken straight from the sheep sheet so the two
# characters share identical scenery.

_sheep = Image.open(os.path.join(REPO, "assets", "sheep_spritesheet.png")).convert("RGBA")

# The flower growth stages are drawn natively rather than lifted from the sheep
# sheet: they are the one piece of scenery an animation actually reaches
# (`flower`, ids 149-153), so upscaled 40px art would sit visibly chunkier than
# everything around it.

STEM = (46, 158, 54, 255)
STEM_DK = (26, 112, 36, 255)
PETAL = (252, 252, 250, 255)
CENTRE = (252, 206, 46, 255)
CENTRE_DK = (222, 156, 20, 255)

_BLOOMS = [(11.5, 13.0, 1.00), (27.5, 9.5, 0.92),
           (22.0, 24.0, 0.86), (9.5, 27.5, 0.80)]


def _daisy(d, cx, cy, scale):
    r = 6.4 * scale
    for k in range(8):
        a = 2 * math.pi * k / 8.0 + 0.2
        px, py = cx + r * 0.66 * math.cos(a), cy + r * 0.66 * math.sin(a)
        R.ell(d, px, py, r * 0.46, r * 0.34, math.degrees(a),
              fill=PETAL, outline=R.OUTLINE, width=1.2)
    R.ell(d, cx, cy, r * 0.34, r * 0.34, fill=CENTRE, outline=R.OUTLINE,
          width=1.1)
    R.ell(d, cx - r * 0.10, cy - r * 0.10, r * 0.14, r * 0.14, fill=CENTRE_DK)


def _flowers(count):
    img = R.canvas()
    d = ImageDraw.Draw(img)
    root = (19.0, 39.0)
    for n, (bx, by, sc) in enumerate(_BLOOMS[:max(2, count)]):
        R.line(d, [root, ((root[0] + bx) / 2.0, (root[1] + by) / 2.0 + 3.0),
                   (bx, by)], STEM_DK, 2.2)
        R.line(d, [root, ((root[0] + bx) / 2.0, (root[1] + by) / 2.0 + 3.0),
                   (bx, by)], STEM, 1.3)
        mx, my = (root[0] + bx) / 2.0, (root[1] + by) / 2.0 + 3.0
        side = -1 if n % 2 else 1
        R.ell(d, mx + side * 3.0, my, 3.0, 1.5, side * 24.0, fill=STEM,
              outline=STEM_DK, width=1.0)
    if count:
        for bx, by, sc in _BLOOMS[:count]:
            _daisy(d, bx, by, sc)
    return img


# 152 is the bare sprout, then one bloom is added per stage up to 153.
put(152, _flowers(0))
put(151, _flowers(1))
put(150, _flowers(2))
put(149, _flowers(3))
put(153, _flowers(4))

# Tile 174 is referenced but intentionally empty (see module docstring).
put(174, Image.new("RGBA", (TILE, TILE), (0, 0, 0, 0)))


# ======================================= tiles the animations never reach ====
# The sheep sheet fills 173 of its 176 cells; only 110 are reachable from
# esheep_animations[]. The rest are poses the original pet's authoring tool
# could address but this port's behaviour graph does not. They are drawn anyway
# so the two sheets stay drop-in interchangeable -- and so a future animation
# edit cannot land on an empty cell. tests/test_spritesheet.py pins this.


# --- turn-around and rear, mirrored ---
put(11, mirror(9))
put(14, mirror(12))
put(131, R.back(foot_phase=0.25, squash=1.02))
put(132, mirror(131))

# --- idle side variants ---
put(56, R.side(eye_state="half", head_dy=0.6))
put(57, R.side(look=(0.0, 0.4), head_dy=0.6))
put(109, R.side(eye_state="closed", head_dy=-0.4, near_ang=16.0))

# --- nibbling something red off the ground ---


def _nibble(stage):
    img = R.side(eye_state="half", head_dy=3.2, head_dx=-1.4, head_ang=-68.0,
                 crouch=1.4, beak_open=0.5 * stage)
    d = ImageDraw.Draw(img)
    R.ell(d, 4.2, 27.0 + stage, 1.6, 1.6, fill=R.RED, outline=R.OUTLINE,
          width=1.2)
    return img


put(52, _nibble(0))
put(53, _nibble(1))

# --- more head-down grazing beats ---
put(71, R.side(eye_state="half", head_dy=3.0, head_dx=-1.3, head_ang=-62.0,
               crouch=1.2, beak_open=0.8))
put(72, R.side(eye_state="half", head_dy=3.0, head_dx=-1.3, head_ang=-62.0,
               crouch=1.2, beak_open=0.2))
put(73, R.side(eye_state="closed", head_dy=2.6, head_dx=-1.1, head_ang=-52.0,
               crouch=1.0))
put(74, R.side(look=(0.0, 0.9), head_dy=2.6, head_dx=-1.1, head_ang=-52.0,
               crouch=1.0))
put(75, R.side(look=(-0.3, 0.9), head_dy=2.8, head_dx=-1.2, head_ang=-56.0,
               crouch=1.1))
put(22, R.side(look=(0.0, 1.0), head_dy=3.2, head_dx=-1.4, head_ang=-72.0,
               crouch=1.6, near_ang=-30.0))

# --- shocked blank stare ---
put(99, R.side(eye_state="shock"))
put(100, R.side(eye_state="shock", head_dy=0.7, lean=-2.0))

# --- crying ---


def _cry(stage):
    img = R.side(eye_state="closed" if stage > 1 else "sad", head_dy=0.8,
                 head_ang=-12.0, beak_open=0.35, crouch=0.4)
    R.tears(ImageDraw.Draw(img), 9.6, 12.0, 1 + stage)
    return img


put(110, _cry(1))
put(111, _cry(2))

# --- front-view expression variants ---

# --- the necktie gag ---
put(54, R.front(tie=True))
put(55, R.front(tie=True, eye_state="half"))

# --- dazed / spiral variants ---
put(86, R.side(eye_state="spiral", head_dy=0.6, near_ang=-18.0))
put(87, mirror(86))
put(88, R.front(eye_state="spiral", lean=-1.0))
put(84, rot(_spin, 135))

# --- pre-rotated wall/ceiling versions of the front and rear poses ---
put(92, rot(R.finish(R.back()), 90))
put(93, rot(R.finish(R.back()), 270))

put(26, rot(R.finish(R.side(look=(0.0, 0.8), head_dy=2.4, head_ang=-44.0)), 180))
put(27, rot(3, 180))

# --- scenery, copied verbatim: none of it is the character ---
# The UFO (158-165), its little pilot (166-168) and the grid overlay (172) are
# props, exactly like the flowers above, and both characters should share them.
for index in (158, 159, 160, 161, 162, 163, 164, 165, 166, 167, 168, 172):
    put(index, from_sheep(index))


# ====================================================== big face closeups ====
# A large head filling the tile with just the toes showing -- the sheep uses
# this framing for every "reaction shot", upright and rotated. Drawing it as a
# small full body instead (which an earlier cut did) loses all the expression.


def _bigface(eye_state="open", blush=False, tie=False):
    img = R.canvas()
    d = ImageDraw.Draw(img)
    cx, cy, hr = 20.0, 18.6, 16.4

    for fx, sign in ((15.4, -1), (24.6, 1)):
        R._foot_side(d, fx, 36.8, sign)

    R.ell(d, cx, cy, hr, hr, fill=R.BODY, outline=R.OUTLINE, width=1.7)
    R.ell(d, cx, cy + hr * 0.20, hr * 0.72, hr * 0.58, fill=R.BELLY)
    for sign in (-1, 1):
        R.eye(d, cx + sign * hr * 0.38, cy - hr * 0.04, 5.6, eye_state)
    R.poly(d, [(cx - 3.6, cy + hr * 0.44), (cx + 3.6, cy + hr * 0.44),
               (cx, cy + hr * 0.88)],
           fill=R.BEAK, outline=R.OUTLINE, width=1.3)
    if blush:
        for sign in (-1, 1):
            R.ell(d, cx + sign * hr * 0.76, cy + hr * 0.34, 3.0, 1.7,
                  fill=R.BLUSH)
    R._gloss(img, cx - hr * 0.34, cy - hr * 0.58, hr * 0.36, hr * 0.20,
             clip=(cx, cy, hr - 1.0))
    R.shade(img, cx, cy, hr, hr)
    return R.finish(img)


_FACE = {st: _bigface(st) for st in ("open", "half", "closed", "sad", "x",
                                     "spiral")}

# upright reaction shots
put(34, _FACE["open"])
put(35, _FACE["half"])
put(36, _FACE["closed"])
put(83, _FACE["closed"])
put(89, _FACE["open"])
put(90, _FACE["open"])
put(101, _FACE["half"])
put(102, _FACE["closed"])
put(115, _bigface("open", blush=True))
put(123, _bigface("closed", blush=True))
put(81, _FACE["half"])
put(82, _bigface("closed", blush=True))
put(119, _FACE["sad"])

# the same head, tumbled. A near-circular head survives any angle, so these
# are free rotations rather than the 90-degree-only ones the body poses need.
put(113, rot(_FACE["open"], 90))
put(117, mirror(113))
put(112, rot(_FACE["open"], 55))
put(118, mirror(112))
put(114, rot(_FACE["half"], 35))
put(116, mirror(114))
put(121, rot(_FACE["open"], 145))
put(125, mirror(121))
put(120, rot(_FACE["half"], 125))
put(126, mirror(120))
put(122, rot(_FACE["closed"], 160))
put(124, mirror(122))
put(91, rot(_FACE["open"], 20))
put(94, rot(_FACE["half"], 180))
put(95, rot(_FACE["open"], 200))
put(85, rot(_FACE["x"], 180))


# ================================================================= write ====

def main():
    sheet = Image.new("RGBA", (COLS * TILE, ROWS * TILE), (0, 0, 0, 0))
    for index, img in tiles.items():
        c, r = index % COLS, index // COLS
        sheet.paste(img, (c * TILE, r * TILE))
    out = os.path.join(REPO, "assets", "penguin_ice_blue_spritesheet.png")
    sheet.save(out)
    preview = sheet.resize((COLS * TILE, ROWS * TILE), Image.NEAREST)
    preview.save(os.path.join(REPO, "assets", "penguin_sheet_preview.png"))
    print("wrote %s (%d tiles)" % (out, len(tiles)))


if __name__ == "__main__":
    main()
