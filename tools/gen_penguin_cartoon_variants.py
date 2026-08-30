#!/usr/bin/env python3
"""Second penguin batch: cartoony/mascot style, modeled on:
  - ~/Pictures/backgrounds/cartoony-tux.jpg  (glossy 3D Tux, googly cross-eyed look)
  - Tux Paint mascot (flat cartoon, big head, forward-looking cheerful eyes)
Same 40x40 tile pipeline as gen_penguins.py: draw big, downsample with NEAREST.
"""
from PIL import Image, ImageDraw

TILE = 40
SCALE = 4
W, H = TILE * SCALE, TILE * SCALE
OUTLINE = (20, 16, 14, 255)


def new_canvas():
    return Image.new("RGBA", (W, H), (0, 0, 0, 0))


def draw_cartoon_penguin(body_color, belly_color, beak_color, foot_color,
                          outline=OUTLINE, gloss=True, cross_eyed=False,
                          bowtie_color=None, wave=False, lean=0, foot_phase=0,
                          ear_patch_color=None, head_scale=1.0):
    img = new_canvas()
    d = ImageDraw.Draw(img)
    cx = W // 2 + lean * 5

    # Chibi proportions: big rounded body, head blends into body (mascot style)
    body_top, body_bot = int(H * 0.30), int(H * 0.95)
    body_w = int(W * 0.36)

    # feet
    foot_y = body_bot - 4
    fdx = 9 * foot_phase
    d.polygon([(cx - 32 + fdx, foot_y), (cx - 4 + fdx, foot_y),
               (cx - 12 + fdx, foot_y + 20), (cx - 28 + fdx, foot_y + 20)],
              fill=foot_color, outline=outline, width=5)
    d.polygon([(cx + 4 - fdx, foot_y), (cx + 32 - fdx, foot_y),
               (cx + 28 - fdx, foot_y + 20), (cx + 12 - fdx, foot_y + 20)],
              fill=foot_color, outline=outline, width=5)

    # body: one big rounded blob (mascot silhouette, no separate neck)
    d.ellipse([cx - body_w, body_top, cx + body_w, body_bot],
               fill=body_color, outline=outline, width=6)

    # wings -- one flat at side, one raised/waving
    if wave:
        wx = cx + body_w - 10
        wy = body_top + 20
        d.ellipse([wx - 4, wy - 34, wx + 30, wy + 6],
                   fill=body_color, outline=outline, width=5)
    else:
        d.ellipse([cx + body_w - 30, body_top + 34, cx + body_w + 8, body_bot - 12],
                   fill=body_color, outline=outline, width=5)
    d.ellipse([cx - body_w - 8, body_top + 34, cx - body_w + 30, body_bot - 12],
               fill=body_color, outline=outline, width=5)

    # belly
    belly_w = int(body_w * 0.68)
    d.ellipse([cx - belly_w, body_top + 40, cx + belly_w, body_bot - 4],
               fill=belly_color, outline=None)

    # head sits high and large, overlapping body top (mascot proportions)
    head_r = int(W * 0.30 * head_scale)
    head_cy = body_top + int(head_r * 0.05)
    d.ellipse([cx - head_r, head_cy - head_r, cx + head_r, head_cy + head_r],
               fill=body_color, outline=outline, width=6)

    # face patch (white mask like real Tux, sits inside the head)
    face_w = int(head_r * 0.86)
    face_h = int(head_r * 0.62)
    face_cy = head_cy + int(head_r * 0.08)
    d.ellipse([cx - face_w, face_cy - face_h, cx + face_w, face_cy + face_h],
               fill=belly_color, outline=None)

    # ear patches (emperor-penguin style flashes behind the eyes)
    if ear_patch_color:
        er = int(head_r * 0.30)
        edx = int(head_r * 0.78)
        ecy = head_cy - int(head_r * 0.02)
        for sign in (-1, 1):
            ex = cx + sign * edx
            d.ellipse([ex - er, ecy - er * 1.3, ex + er, ecy + er * 1.3],
                      fill=ear_patch_color)

    # eyes: big white ovals, pupils either cross-eyed (looking at beak) or forward
    eye_r = int(head_r * 0.40)
    eye_dx = int(head_r * 0.42)
    eye_cy = head_cy - int(head_r * 0.06)
    for sign in (-1, 1):
        ex = cx + sign * eye_dx
        d.ellipse([ex - eye_r, eye_cy - eye_r, ex + eye_r, eye_cy + eye_r],
                   fill=(255, 255, 255, 255), outline=outline, width=4)
        pr = int(eye_r * 0.46)
        if cross_eyed:
            # pupils rotated inward/down toward the beak, like the glossy reference
            pdx = -sign * int(eye_r * 0.32)
            pdy = int(eye_r * 0.28)
        else:
            pdx = int(eye_r * 0.05) * sign
            pdy = -int(eye_r * 0.05)
        px, py = ex + pdx, eye_cy + pdy
        d.ellipse([px - pr, py - pr, px + pr, py + pr], fill=(15, 12, 12, 255))
        hr = max(2, pr // 3)
        d.ellipse([px - pr + 2, py - pr + 2, px - pr + 2 + hr * 2, py - pr + 2 + hr * 2],
                   fill=(255, 255, 255, 255))

    # beak: bigger, more triangular/cartoony
    beak_w = int(head_r * 0.62)
    beak_h = int(head_r * 0.40)
    beak_cy = head_cy + int(head_r * 0.42)
    d.polygon([(cx - beak_w // 2, beak_cy - beak_h // 2),
               (cx + beak_w // 2, beak_cy - beak_h // 2),
               (cx, beak_cy + beak_h)],
              fill=beak_color, outline=outline)

    # bowtie accessory
    if bowtie_color:
        by = body_top + int(head_r * 1.15)
        d.polygon([(cx - 22, by - 12), (cx - 3, by), (cx - 22, by + 12)],
                   fill=bowtie_color, outline=outline)
        d.polygon([(cx + 22, by - 12), (cx + 3, by), (cx + 22, by + 12)],
                   fill=bowtie_color, outline=outline)
        d.ellipse([cx - 6, by - 7, cx + 6, by + 7], fill=bowtie_color, outline=outline)

    # gloss highlight (soft light patch on head, like a 3D-rendered toy)
    if gloss:
        gw, gh = int(head_r * 0.5), int(head_r * 0.32)
        gx, gy = cx - int(head_r * 0.35), head_cy - int(head_r * 0.55)
        overlay = Image.new("RGBA", img.size, (0, 0, 0, 0))
        od = ImageDraw.Draw(overlay)
        od.ellipse([gx - gw, gy - gh, gx + gw, gy + gh], fill=(255, 255, 255, 70))
        img.alpha_composite(overlay)

    return img.resize((TILE, TILE), Image.NEAREST)


def sheet(frames, path):
    n = len(frames)
    out = Image.new("RGBA", (TILE * n, TILE), (0, 0, 0, 0))
    for i, f in enumerate(frames):
        out.paste(f, (i * TILE, 0), f)
    out.save(path)
    preview = out.resize((TILE * n * 8, TILE * 8), Image.NEAREST)
    preview.save(str(path).replace(".png", "_preview.png"))


import os
d = os.path.join(os.path.dirname(__file__), "..", "assets", "penguin_drafts")
os.makedirs(d, exist_ok=True)

# --- Variant D: Glossy Classic Tux (homage to cartoony-tux.jpg's googly cross-eyed look) ---
d_frames = [
    draw_cartoon_penguin((18, 18, 20, 255), (250, 250, 248, 255), (245, 190, 40, 255),
                          (245, 190, 40, 255), cross_eyed=True, gloss=True, lean=0, foot_phase=0),
    draw_cartoon_penguin((18, 18, 20, 255), (250, 250, 248, 255), (245, 190, 40, 255),
                          (245, 190, 40, 255), cross_eyed=True, gloss=True, lean=-1, foot_phase=1),
    draw_cartoon_penguin((18, 18, 20, 255), (250, 250, 248, 255), (245, 190, 40, 255),
                          (245, 190, 40, 255), cross_eyed=True, gloss=True, lean=1, foot_phase=-1),
]
sheet(d_frames, f"{d}/penguin_d_glossytux.png")

# --- Variant E: Tux Paint mascot style -- flatter, forward gaze, bigger head, waving wing ---
e_frames = [
    draw_cartoon_penguin((30, 30, 32, 255), (255, 253, 247, 255), (255, 150, 40, 255),
                          (255, 150, 40, 255), cross_eyed=False, gloss=False, wave=True,
                          lean=0, foot_phase=0),
    draw_cartoon_penguin((30, 30, 32, 255), (255, 253, 247, 255), (255, 150, 40, 255),
                          (255, 150, 40, 255), cross_eyed=False, gloss=False, wave=True,
                          lean=-1, foot_phase=1),
    draw_cartoon_penguin((30, 30, 32, 255), (255, 253, 247, 255), (255, 150, 40, 255),
                          (255, 150, 40, 255), cross_eyed=False, gloss=False, wave=True,
                          lean=1, foot_phase=-1),
]
sheet(e_frames, f"{d}/penguin_e_tuxpaint.png")

# --- Variant F: Bowtie Buddy -- playful original cartoony mascot ---
f_frames = [
    draw_cartoon_penguin((25, 25, 40, 255), (255, 251, 240, 255), (250, 170, 60, 255),
                          (250, 170, 60, 255), cross_eyed=False, gloss=True,
                          bowtie_color=(210, 40, 50, 255), lean=0, foot_phase=0),
    draw_cartoon_penguin((25, 25, 40, 255), (255, 251, 240, 255), (250, 170, 60, 255),
                          (250, 170, 60, 255), cross_eyed=False, gloss=True,
                          bowtie_color=(210, 40, 50, 255), lean=-1, foot_phase=1),
    draw_cartoon_penguin((25, 25, 40, 255), (255, 251, 240, 255), (250, 170, 60, 255),
                          (250, 170, 60, 255), cross_eyed=False, gloss=True,
                          bowtie_color=(210, 40, 50, 255), lean=1, foot_phase=-1),
]
sheet(f_frames, f"{d}/penguin_f_bowtie.png")

print("done")

# --- Variant G: Glossy Classic, Ice Blue recolor (same rig as D) ---
g_frames = [
    draw_cartoon_penguin((30, 45, 70, 255), (250, 250, 248, 255), (245, 190, 40, 255),
                          (245, 190, 40, 255), cross_eyed=True, gloss=True, lean=0, foot_phase=0),
    draw_cartoon_penguin((30, 45, 70, 255), (250, 250, 248, 255), (245, 190, 40, 255),
                          (245, 190, 40, 255), cross_eyed=True, gloss=True, lean=-1, foot_phase=1),
    draw_cartoon_penguin((30, 45, 70, 255), (250, 250, 248, 255), (245, 190, 40, 255),
                          (245, 190, 40, 255), cross_eyed=True, gloss=True, lean=1, foot_phase=-1),
]
sheet(g_frames, f"{d}/penguin_g_iceblue.png")

# --- Variant H: Glossy Classic, Emperor ear patches (same rig as D) ---
h_frames = [
    draw_cartoon_penguin((18, 18, 20, 255), (250, 250, 248, 255), (245, 190, 40, 255),
                          (245, 190, 40, 255), cross_eyed=True, gloss=True,
                          ear_patch_color=(250, 195, 60, 255), lean=0, foot_phase=0),
    draw_cartoon_penguin((18, 18, 20, 255), (250, 250, 248, 255), (245, 190, 40, 255),
                          (245, 190, 40, 255), cross_eyed=True, gloss=True,
                          ear_patch_color=(250, 195, 60, 255), lean=-1, foot_phase=1),
    draw_cartoon_penguin((18, 18, 20, 255), (250, 250, 248, 255), (245, 190, 40, 255),
                          (245, 190, 40, 255), cross_eyed=True, gloss=True,
                          ear_patch_color=(250, 195, 60, 255), lean=1, foot_phase=-1),
]
sheet(h_frames, f"{d}/penguin_h_emperor.png")

# --- Variant I: Glossy Classic, Baby Tux -- bigger head, softer charcoal-grey down (same rig as D) ---
i_frames = [
    draw_cartoon_penguin((60, 58, 56, 255), (252, 248, 240, 255), (240, 175, 70, 255),
                          (240, 175, 70, 255), cross_eyed=True, gloss=True, head_scale=1.12,
                          lean=0, foot_phase=0),
    draw_cartoon_penguin((60, 58, 56, 255), (252, 248, 240, 255), (240, 175, 70, 255),
                          (240, 175, 70, 255), cross_eyed=True, gloss=True, head_scale=1.12,
                          lean=-1, foot_phase=1),
    draw_cartoon_penguin((60, 58, 56, 255), (252, 248, 240, 255), (240, 175, 70, 255),
                          (240, 175, 70, 255), cross_eyed=True, gloss=True, head_scale=1.12,
                          lean=1, foot_phase=-1),
]
sheet(i_frames, f"{d}/penguin_i_baby.png")

print("g/h/i done")
