#!/usr/bin/env python3
"""Ice Blue penguin drawing rig.

Everything is drawn in *tile space* -- a 40x40 box whose origin is the top-left
of the tile and whose y=40 line is the ground the character stands on -- and
supersampled by ``S`` before being point-sampled back down to 40x40. Point
sampling (NEAREST) is deliberate: it keeps hard pixel edges, which is what the
sheep sheet this has to sit alongside looks like.

The rig is a *side view facing left*, matching the sheep sheet's authored
direction; the runtime mirrors the tile horizontally when the character walks
the other way (see ``sprite_is_flipped`` in src/main.c). Front, three-quarter
and rear views exist too, because the turn-around, drag and sleep animations
need them.
"""
import math

from PIL import Image, ImageChops, ImageDraw

# Art is authored in a fixed 40-unit coordinate space regardless of how many
# pixels a tile ends up being, so a resolution change is a one-line edit here
# and every pose keeps its proportions.
UNITS = 40.0
TILE = 80          # output pixels per tile
S = 8              # supersample factor over the output
W = H = TILE * S
PPU = W / UNITS    # device pixels per authoring unit

# --- Ice Blue palette (variant G of tools/gen_penguin_cartoon_variants.py) ---
BODY = (48, 72, 112, 255)
BODY_DK = (28, 44, 72, 255)
BODY_LT = (78, 112, 162, 255)
BELLY = (250, 250, 248, 255)
BELLY_SH = (216, 226, 238, 255)
BEAK = (245, 190, 40, 255)
BEAK_DK = (198, 142, 24, 255)
FOOT = (245, 190, 40, 255)
FOOT_DK = (198, 142, 24, 255)
OUTLINE = (0, 0, 0, 255)
WHITE = (255, 255, 255, 255)
PUPIL = (16, 14, 18, 255)
BLUSH = (228, 116, 126, 255)
RED = (208, 44, 48, 255)
SOOT = (56, 52, 50, 255)
SOOT_LT = (112, 106, 102, 255)
FLAME = [(255, 226, 90, 255), (250, 152, 34, 255), (226, 62, 30, 255)]
WATER = (96, 208, 240, 255)
GLASS = (196, 214, 230, 255)


def canvas():
    return Image.new("RGBA", (W, H), (0, 0, 0, 0))


def finish(img):
    """Supersampled canvas -> crisp 40x40 tile."""
    return img.resize((TILE, TILE), Image.NEAREST)


def _s(pts):
    return [(x * PPU, y * PPU) for x, y in pts]


def poly(d, pts, fill=None, outline=None, width=1.0):
    d.polygon(_s(pts), fill=fill, outline=outline, width=max(1, int(width * PPU)))


def ell(d, cx, cy, rx, ry, ang=0.0, fill=None, outline=None, width=1.0, n=64):
    """Ellipse, optionally rotated, in tile space."""
    a = math.radians(ang)
    ca, sa = math.cos(a), math.sin(a)
    pts = []
    for i in range(n):
        t = 2 * math.pi * i / n
        x, y = rx * math.cos(t), ry * math.sin(t)
        pts.append((cx + x * ca - y * sa, cy + x * sa + y * ca))
    poly(d, pts, fill=fill, outline=outline, width=width)


def line(d, pts, fill, width=1.0):
    d.line(_s(pts), fill=fill, width=max(1, int(width * PPU)), joint="curve")


def rot(pt, cx, cy, ang):
    a = math.radians(ang)
    dx, dy = pt[0] - cx, pt[1] - cy
    return (cx + dx * math.cos(a) - dy * math.sin(a),
            cy + dx * math.sin(a) + dy * math.cos(a))


# ---------------------------------------------------------------- eyes ------

def eye(d, cx, cy, r, state="open", look=(0.0, 0.0), lash=False):
    """One eye. ``look`` nudges the pupil, in eye radii."""
    if state == "closed":
        line(d, [(cx - r, cy), (cx - r * 0.4, cy + r * 0.45), (cx + r * 0.4, cy + r * 0.45),
                 (cx + r, cy)], OUTLINE, 0.9)
        return
    if state == "x":
        line(d, [(cx - r * 0.8, cy - r * 0.8), (cx + r * 0.8, cy + r * 0.8)], OUTLINE, 0.9)
        line(d, [(cx + r * 0.8, cy - r * 0.8), (cx - r * 0.8, cy + r * 0.8)], OUTLINE, 0.9)
        return
    if state == "shock":
        ell(d, cx, cy, r * 1.15, r * 1.15, fill=WHITE, outline=OUTLINE, width=1.3)
        ell(d, cx, cy, r * 0.20, r * 0.20, fill=PUPIL)
        return
    if state == "spiral":
        ell(d, cx, cy, r, r, fill=WHITE, outline=OUTLINE, width=1.3)
        pts = []
        for i in range(34):
            t = i / 33.0
            ang = t * math.pi * 3.2
            rad = r * 0.88 * (1.0 - t)
            pts.append((cx + rad * math.cos(ang), cy + rad * math.sin(ang)))
        line(d, pts, OUTLINE, 0.7)
        return

    ell(d, cx, cy, r, r, fill=WHITE, outline=OUTLINE, width=1.3)
    pr = r * 0.46
    px, py = cx + look[0] * r * 0.42, cy + look[1] * r * 0.42
    ell(d, px, py, pr, pr, fill=PUPIL)
    ell(d, px - pr * 0.35, py - pr * 0.4, pr * 0.36, pr * 0.36, fill=WHITE)

    if state == "half":
        poly(d, [(cx - r - 0.4, cy - r - 0.4), (cx + r + 0.4, cy - r - 0.4),
                 (cx + r + 0.4, cy - r * 0.15), (cx - r - 0.4, cy - r * 0.15)], fill=BODY)
        line(d, [(cx - r, cy - r * 0.15), (cx + r, cy - r * 0.15)], OUTLINE, 0.6)
    elif state == "sad":
        poly(d, [(cx - r - 0.4, cy - r - 0.4), (cx + r + 0.4, cy - r - 0.4),
                 (cx + r + 0.4, cy - r * 0.3), (cx - r - 0.4, cy - r * 0.55)], fill=BODY)
        line(d, [(cx - r, cy - r * 0.55), (cx + r, cy - r * 0.3)], OUTLINE, 0.6)
    if lash:
        line(d, [(cx - r * 0.2, cy - r * 1.25), (cx + r * 0.9, cy - r * 1.05)], OUTLINE, 0.6)


# ---------------------------------------------------------------- parts -----

def _foot_side(d, x, y, dirn=-1, lift=0.0, ang=0.0, color=FOOT):
    """Webbed foot seen from the side, toes pointing ``dirn``."""
    pts = [(x, y - 2.6 - lift), (x + dirn * 1.8, y - 2.4 - lift),
           (x + dirn * 6.2, y + 0.4 - lift), (x + dirn * 5.8, y + 1.5 - lift),
           (x - dirn * 1.8, y + 1.5 - lift), (x - dirn * 1.2, y - 1.2 - lift)]
    if ang:
        pts = [rot(p, x, y - lift, ang) for p in pts]
    poly(d, pts, fill=color, outline=OUTLINE, width=1.3)
    if color == FOOT:
        toe = [pts[2], pts[3], pts[4]]
        poly(d, toe, fill=FOOT_DK)


def _flipper(d, cx, cy, ang, scale=1.0, color=BODY, outline=OUTLINE):
    ell(d, cx, cy, 3.6 * scale, 7.0 * scale, ang, fill=color, outline=outline, width=1.4)


def _beak_side(d, tipx, tipy, opening=0.0, size=1.0, color=BEAK, ang=0.0):
    """Wedge beak pointing left; ``opening`` splits it into upper/lower jaw.

    ``ang`` swings the whole beak about its base, so a lowered head can point
    its beak at the ground instead of straight ahead.
    """
    bx = tipx + 5.6 * size
    up = opening * 2.1
    upper = [(tipx, tipy - up * 0.5), (bx, tipy - 2.5 * size), (bx, tipy + 0.2 * size)]
    lower = [(tipx, tipy + up * 0.6), (bx, tipy + 0.2 * size), (bx, tipy + 2.4 * size)]
    if ang:
        upper = [rot(p, bx, tipy, ang) for p in upper]
        lower = [rot(p, bx, tipy, ang) for p in lower]
    poly(d, upper, fill=color, outline=OUTLINE, width=1.3)
    poly(d, lower, fill=BEAK_DK if color == BEAK else color, outline=OUTLINE,
         width=1.3)


def shade(img, cx, cy, rx, ry, strength=1.0):
    """Model the flat fills into volume.

    Builds a soft form shadow toward the lower right and a rim light toward the
    upper left out of stacked translucent ellipses, then clips the whole thing
    to the sprite's own silhouette. Compositing happens at 8x, before the
    downsample, so the result lands as a handful of discrete tones rather than a
    smooth ramp -- which is what the sheep sheet does and what reads as pixel
    art. Without this the body is one flat colour and looks cheap next to it.
    """
    mask = img.getchannel("A").point(lambda v: 255 if v > 0 else 0)
    ov = Image.new("RGBA", img.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(ov)

    steps = 5
    for k in range(steps):
        t = (k + 1) / float(steps)
        ell(d, cx + rx * 0.30 * t, cy + ry * 0.34 * t,
            rx * (1.06 - 0.16 * t), ry * (1.06 - 0.16 * t),
            fill=(0, 0, 0, int(16 * strength)))
    for k in range(steps):
        t = (k + 1) / float(steps)
        ell(d, cx - rx * (0.30 + 0.16 * t), cy - ry * (0.34 + 0.14 * t),
            rx * 0.60 * (1.0 - 0.14 * t), ry * 0.52 * (1.0 - 0.14 * t),
            fill=(255, 255, 255, int(13 * strength)))

    ov.putalpha(ImageChops.multiply(ov.getchannel("A"), mask))
    img.alpha_composite(ov)


def _gloss(img, cx, cy, rx, ry, alpha=24, clip=None):
    """Soft sheen patch, optionally clipped to a (cx, cy, r) circle."""
    overlay = Image.new("RGBA", img.size, (0, 0, 0, 0))
    ell(ImageDraw.Draw(overlay), cx, cy, rx, ry, fill=(255, 255, 255, alpha))
    if clip:
        mask = Image.new("L", img.size, 0)
        ell(ImageDraw.Draw(mask), clip[0], clip[1], clip[2], clip[2], fill=255)
        overlay.putalpha(Image.composite(overlay.getchannel("A"),
                                         Image.new("L", img.size, 0), mask))
    img.alpha_composite(overlay)


# ------------------------------------------------------------ side view -----

def side(lean=0.0, head_dx=0.0, head_dy=0.0, head_ang=0.0, eye_state="open",
         look=(0.0, 0.0), beak_open=0.0, near_ang=8.0, far_ang=-6.0,
         foot_phase=0.0, crouch=0.0, squash=1.0, body_color=BODY,
         belly_color=BELLY, beak_color=BEAK, gloss=True, blush=False,
         tail=True, outline=OUTLINE, wing_up=False):
    """The workhorse pose: penguin in profile, facing left.

    ``foot_phase`` in [-1, 1] swings the legs; ``crouch`` sinks the body toward
    the ground; ``squash`` scales the body vertically about the feet.
    """
    img = canvas()
    d = ImageDraw.Draw(img)

    ground = 38.6
    brx, bry = 14.0, 12.8 * squash
    bcx = 21.4 + lean * 0.6
    bcy = ground - 2.6 - bry + crouch

    hr = 10.6
    hx = 13.4 + head_dx + lean * 0.6
    hy = bcy - bry * 0.90 + head_dy

    # --- legs/feet: far leg first (darker) so the pair reads as two ---
    for sx, ph, col in ((22.2, -foot_phase, FOOT_DK), (14.6, foot_phase, FOOT)):
        lift = max(0.0, ph) * 3.2
        fx = sx + ph * 3.8
        line(d, [(fx + 1.0, bcy + bry * 0.50), (fx + 1.0, ground - 1.0 - lift)],
             col, 1.6)
        _foot_side(d, fx, ground - lift, -1, color=col)

    # --- far flipper (behind the body) ---
    _flipper(d, bcx + brx * 0.34, bcy + 1.4, far_ang, 0.82, BODY_DK, OUTLINE)

    # --- body ---
    ell(d, bcx, bcy, brx, bry, lean * 1.1,
        fill=body_color, outline=outline, width=1.7)
    if tail:
        poly(d, [(bcx + brx * 0.58, bcy + bry * 0.62),
                 (bcx + brx * 1.10, bcy + bry * 0.88),
                 (bcx + brx * 0.54, bcy + bry * 0.96)],
             fill=body_color, outline=outline, width=1.3)

    # --- belly (front/lower, cream) ---
    ell(d, bcx - brx * 0.26, bcy + bry * 0.14, brx * 0.70, bry * 0.80,
        lean * 1.1, fill=belly_color)

    # --- head, blended into the body ---
    ell(d, hx, hy, hr, hr, fill=body_color, outline=outline, width=1.7)
    # re-seat the neck so head and body read as one silhouette
    ell(d, hx + 4.2, hy + hr * 0.66, hr * 0.66, hr * 0.62, fill=body_color)

    # face patch: cream wedge over the front of the head, joined to the belly
    fa = math.radians(head_ang * 0.4)
    fpx = hx - hr * 0.34 * math.cos(fa) + hr * 0.14 * math.sin(fa)
    fpy = hy + hr * 0.26 * math.cos(fa) + hr * 0.34 * math.sin(fa)
    ell(d, fpx, fpy, hr * 0.66, hr * 0.52, head_ang * 0.4, fill=belly_color)

    # --- beak ---
    btx, bty = rot((hx - hr * 1.02, hy + hr * 0.20), hx, hy, head_ang * 0.4)
    _beak_side(d, btx, bty, beak_open, 1.0, beak_color, ang=head_ang)

    # --- eye ---
    ex, ey = rot((hx - hr * 0.24, hy - hr * 0.24), hx, hy, head_ang * 0.4)
    eye(d, ex, ey, 4.0, eye_state, look)

    if blush:
        ell(d, ex - 1.6, ey + 4.2, 2.4, 1.3, fill=BLUSH)

    # --- near flipper, over the belly so it reads as a separate limb ---
    if wing_up:
        _flipper(d, bcx - brx * 0.22, bcy - bry * 1.02, -74.0, 0.92,
                 body_color, outline)
    else:
        _flipper(d, bcx + brx * 0.06, bcy + bry * 0.14, near_ang, 0.86,
                 body_color, outline)

    if gloss:
        _gloss(img, hx - hr * 0.26, hy - hr * 0.54, hr * 0.40, hr * 0.24,
               clip=(hx, hy, hr - 0.9))
    shade(img, bcx, bcy, brx, bry)
    return img


# ----------------------------------------------------------- front view -----

def front(eye_state="open", look=(0.0, 0.0), wing_up=False, foot_phase=0.0,
          body_color=BODY, belly_color=BELLY, beak_color=BEAK, blush=False,
          gloss=True, squash=1.0, arms_up=False, outline=OUTLINE, lean=0.0,
          tie=False):
    img = canvas()
    d = ImageDraw.Draw(img)

    ground = 39.5
    brx, bry = 13.6, 13.4 * squash
    cx = 20.0 + lean
    bcy = ground - 3.4 - bry

    _foot_side(d, cx - 2.4 + foot_phase * 2.0, ground, -1)
    _foot_side(d, cx + 8.4 - foot_phase * 2.0, ground, 1)

    ell(d, cx, bcy, brx, bry, fill=body_color, outline=outline, width=1.7)

    if arms_up:
        _flipper(d, cx - brx + 1.0, bcy - 6.0, 42.0, 0.95, body_color, outline)
        _flipper(d, cx + brx - 1.0, bcy - 6.0, -42.0, 0.95, body_color, outline)
    elif wing_up:
        _flipper(d, cx - brx + 1.4, bcy + 1.0, -12.0, 0.95, body_color, outline)
        _flipper(d, cx + brx - 1.4, bcy - 5.4, -58.0, 0.95, body_color, outline)
    else:
        _flipper(d, cx - brx + 1.4, bcy + 1.0, -12.0, 0.95, body_color, outline)
        _flipper(d, cx + brx - 1.4, bcy + 1.0, 12.0, 0.95, body_color, outline)

    ell(d, cx, bcy + bry * 0.20, brx * 0.66, bry * 0.76, fill=belly_color)

    hr = 10.6
    hy = bcy - bry * 0.86
    ell(d, cx, hy, hr, hr, fill=body_color, outline=outline, width=1.7)
    ell(d, cx, hy + hr * 0.18, hr * 0.84, hr * 0.62, fill=belly_color)

    er = 3.8
    for sign in (-1, 1):
        eye(d, cx + sign * hr * 0.42, hy - hr * 0.10, er, eye_state,
            (look[0], look[1]))
    if blush:
        for sign in (-1, 1):
            ell(d, cx + sign * hr * 0.86, hy + hr * 0.30, 2.2, 1.2, fill=BLUSH)

    bw, bh = hr * 0.36, hr * 0.44
    by = hy + hr * 0.46
    poly(d, [(cx - bw, by), (cx + bw, by), (cx, by + bh)],
         fill=beak_color, outline=outline, width=1.3)

    if tie:
        # lapels cut into the belly, with a knotted tie between them
        ty = bcy - bry * 0.44
        for sign in (-1, 1):
            poly(d, [(cx + sign * 1.2, ty - 1.4),
                     (cx + sign * 8.2, ty - 0.6),
                     (cx + sign * 3.2, ty + 9.0)],
                 fill=body_color, outline=outline, width=1.3)
        poly(d, [(cx - 2.0, ty - 0.4), (cx + 2.0, ty - 0.4),
                 (cx + 1.4, ty + 2.4), (cx - 1.4, ty + 2.4)],
             fill=RED, outline=outline, width=1.2)
        poly(d, [(cx - 2.4, ty + 2.4), (cx + 2.4, ty + 2.4),
                 (cx, ty + 9.4)], fill=RED, outline=outline, width=1.2)

    if gloss:
        _gloss(img, cx - hr * 0.34, hy - hr * 0.52, hr * 0.40, hr * 0.24,
               clip=(cx, hy, hr - 0.9))
    shade(img, cx, bcy, brx, bry)
    return img


# ------------------------------------------------------------ rear view -----

def back(foot_phase=0.0, legs_up=False, tail_side=0.0, body_color=BODY,
         gloss=True, outline=OUTLINE, squash=1.0, wings_out=False):
    """Seen from behind: all body, no face. Used by drag/sleep/relief frames."""
    img = canvas()
    d = ImageDraw.Draw(img)

    ground = 39.5
    brx, bry = 13.0, 12.6 * squash
    cx = 20.0
    bcy = ground - 3.4 - bry

    if legs_up:
        bcy += 2.0
    else:
        _foot_side(d, cx - 2.4 + foot_phase * 2.4, ground, -1)
        _foot_side(d, cx + 8.4 - foot_phase * 2.4, ground, 1)

    if wings_out:
        _flipper(d, cx - brx - 0.6, bcy - 1.0, -34.0, 0.95, body_color, outline)
        _flipper(d, cx + brx + 0.6, bcy - 1.0, 34.0, 0.95, body_color, outline)
    else:
        _flipper(d, cx - brx + 1.2, bcy + 0.6, -10.0, 0.95, body_color, outline)
        _flipper(d, cx + brx - 1.2, bcy + 0.6, 10.0, 0.95, body_color, outline)

    ell(d, cx, bcy, brx, bry, fill=body_color, outline=outline, width=1.7)

    hr = 9.2
    hy = bcy - bry * 0.82
    ell(d, cx, hy, hr, hr, fill=body_color, outline=outline, width=1.7)

    # nape seam + stubby tail so the rear reads as a rear
    line(d, [(cx, hy + hr * 0.55), (cx, bcy - bry * 0.20)], BODY_DK, 0.55)
    poly(d, [(cx - 2.4 + tail_side, bcy + bry * 0.72),
             (cx + 2.4 + tail_side, bcy + bry * 0.72),
             (cx + tail_side, bcy + bry * 1.02)],
         fill=body_color, outline=outline, width=1.3)

    if legs_up:
        # legs kicking up past the shoulders, drawn last so they stay visible
        for sign, kick in ((-1, -1.6), (1, 1.6)):
            fx = cx + sign * 9.4
            line(d, [(fx - sign * 2.0, bcy - bry * 0.10),
                     (fx, bcy - bry * 0.62),
                     (fx + kick, bcy - bry * 1.16)], FOOT_DK, 2.0)
            _foot_side(d, fx + kick, bcy - bry * 1.16, sign, ang=sign * 96)

    if gloss:
        _gloss(img, cx - hr * 0.34, hy - hr * 0.50, hr * 0.42, hr * 0.26,
               clip=(cx, hy, hr - 0.9))
    shade(img, cx, bcy, brx, bry)
    return img


# ------------------------------------------------------ three-quarter -------

def three_quarter(eye_state="open", foot_phase=0.0, gloss=True):
    """Midpoint of the turn-around: body angled, both eyes visible."""
    img = canvas()
    d = ImageDraw.Draw(img)

    ground = 39.5
    brx, bry = 13.6, 13.0
    cx = 20.6
    bcy = ground - 3.4 - bry

    _foot_side(d, cx - 3.4 + foot_phase * 2.0, ground, -1)
    _foot_side(d, cx + 7.4 - foot_phase * 2.0, ground, -1)

    _flipper(d, cx + brx - 0.4, bcy + 1.0, 16.0, 0.92, BODY_DK, OUTLINE)
    ell(d, cx, bcy, brx, bry, fill=BODY, outline=OUTLINE, width=1.7)
    ell(d, cx - 3.0, bcy + bry * 0.18, brx * 0.62, bry * 0.76, fill=BELLY)

    hr = 10.4
    hx, hy = cx - 2.8, bcy - bry * 0.86
    ell(d, hx, hy, hr, hr, fill=BODY, outline=OUTLINE, width=1.7)
    ell(d, hx - 1.4, hy + hr * 0.18, hr * 0.76, hr * 0.58, fill=BELLY)

    eye(d, hx - hr * 0.60, hy - hr * 0.14, 3.8, eye_state, (-0.5, 0.0))
    eye(d, hx + hr * 0.34, hy - hr * 0.14, 3.2, eye_state, (-0.5, 0.0))

    btx = hx - hr * 0.92
    _beak_side(d, btx - 1.2, hy + hr * 0.42, 0.0, 0.9)

    _flipper(d, cx - brx + 1.0, bcy + 1.4, -14.0, 0.95, BODY, OUTLINE)
    if gloss:
        _gloss(img, hx - hr * 0.30, hy - hr * 0.52, hr * 0.40, hr * 0.24,
               clip=(hx, hy, hr - 0.9))
    shade(img, cx, bcy, brx, bry)
    return img


# ------------------------------------------------------------- overlays -----

def sweat(d, x, y, size=1.0):
    poly(d, [(x, y - 3.4 * size), (x + 2.0 * size, y + 0.8 * size),
             (x, y + 2.4 * size), (x - 2.0 * size, y + 0.8 * size)],
         fill=WHITE, outline=OUTLINE, width=1.2)


def tears(d, x, y, count=2, size=1.0):
    """Droplets running down from an eye."""
    for i in range(count):
        t = i / float(max(1, count))
        ell(d, x - t * 1.4, y + 2.6 + t * 5.0, 1.3 * size, 1.7 * size,
            fill=WATER, outline=OUTLINE, width=1.2)


def flame_layer(cx, cy, rx, ry, intensity=1.0, seed=0):
    """A blaze envelope on its own canvas, to composite *behind* the body.

    Three nested wobbly rings (red outside, yellow inside) with the wobble
    biased upward, so the flames stream off the top the way fire does.
    """
    img = canvas()
    d = ImageDraw.Draw(img)
    for layer, col, scale in ((0, FLAME[2], 1.00), (1, FLAME[1], 0.84),
                              (2, FLAME[0], 0.64)):
        pts = []
        n = 72
        for i in range(n):
            t = 2 * math.pi * i / n
            up = max(0.0, -math.sin(t))
            wob = 0.5 + 0.5 * math.sin(t * 7.0 + layer * 2.3 + seed * 1.7)
            k = scale * (1.0 + intensity * (0.10 + 0.66 * up) * (0.40 + 0.60 * wob))
            pts.append((cx + rx * k * math.cos(t), cy + ry * k * math.sin(t)))
        poly(d, pts, fill=col)
    return img


def flame_tongues(d, cx, cy, rx, ry, intensity=1.0, seed=0):
    """A few licks drawn in *front* of the body, so it looks engulfed."""
    rnd = random_stream(seed + 97)
    for _ in range(int(4 + 5 * intensity)):
        t = -math.pi * (0.05 + 0.90 * rnd())
        bx, by = cx + rx * 0.92 * math.cos(t), cy + ry * 0.92 * math.sin(t)
        h = (3.0 + 4.0 * rnd()) * intensity
        w = 1.4 + 1.2 * rnd()
        col = FLAME[int(rnd() * 3) % 3]
        poly(d, [(bx - w, by + w * 0.6), (bx + w, by + w * 0.6), (bx, by - h)],
             fill=col)


def soot(d, cx, cy, rx, ry, density=90, seed=0):
    rnd = random_stream(seed)
    for _ in range(density):
        a = 2 * math.pi * rnd()
        r = math.sqrt(rnd())
        x = cx + rx * r * math.cos(a)
        y = cy + ry * r * math.sin(a)
        c = SOOT_LT if rnd() > 0.6 else SOOT
        ell(d, x, y, 0.7, 0.7, fill=c, n=6)


def random_stream(seed):
    state = [seed * 6364136223846793005 + 1442695040888963407]

    def nxt():
        state[0] = (state[0] * 6364136223846793005 + 1442695040888963407) & ((1 << 64) - 1)
        return ((state[0] >> 33) & 0xFFFFFF) / float(0xFFFFFF)

    return nxt
