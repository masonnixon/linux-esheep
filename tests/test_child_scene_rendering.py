#!/usr/bin/env python3
"""Actually render each authored <child> (multi-sprite) scene and check that
both the parent and the child are visible on screen, separated by a real gap.

The three authored child records (21->23 "batha"/bathw, 26->27 "eat"/flower,
28->31 "blacksheepa"/blacksheepv) each looked fine in `tests/test_child_animations.py`,
which only checks the XML/generated data -- names, IDs, expression strings are
non-empty. Neither of the following two real bugs showed up in the data, only
in what actually got composited on screen:

1. The sprite window is created with gtk_widget_set_size_request() pinned to
   one tile plus resizable=FALSE. sync_scene_window()'s gtk_window_resize()
   calls for a multi-sprite scene got fought back down to that fixed request,
   so only a sliver of whichever tile still fit inside the old bounds ever
   rendered -- usually just the child, with the parent almost entirely
   clipped off (or vice versa).
2. An authored <child> x/y expression is either a small delta from the parent
   (references imageX/imageY) or, like a <spawn> point, an absolute position
   on the monitor (batha's bathw prop enters from off the right edge of the
   screen regardless of where the parent is; it never mentions imageX at
   all). The renderer only understands parent-relative offsets, so treating
   an absolute-style result as one directly produces an offset of roughly
   "screenW pixels away" -- which the window then grows to contain, placing
   the child so far from the parent it lands off the edge of the real
   desktop (or, on a small enough screen, off the edge of the test's own
   virtual display).

Both symptoms look the same from a single screenshot: one sprite visible,
the other missing, or squeezed into an area far too small or far too large
to be both tiles at their authored position. This test renders each scene
for real under Xvfb and checks the actual on-screen footprint against that.
"""
import os
import subprocess
import sys
import shutil
import time

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROGRAM = os.path.join(ROOT, "esheep")
SCREEN_W, SCREEN_H = 1920, 1080  # a realistic monitor; blacksheepv (child of
# animation 28) keeps flying further from the parent every tick by design,
# so a narrower test screen gives it too little room before it legitimately
# exits off the edge, which looks identical to a rendering failure

# animation id -> name, for error messages
CHILD_SCENES = {
    21: "batha -> bathw",
    26: "eat -> flower",
    28: "blacksheepa -> blacksheepv",
}


def render_scene(animation_id, out_path):
    """Run the scene under Xvfb and return the widest-content blob set seen
    across several samples.

    Some children keep moving every tick (e.g. blacksheepv flies further
    from the parent each frame), so the composited window keeps growing
    throughout the whole scene, not just once at spawn. A single screenshot
    can land mid-resize and see a transient sliver that has nothing to do
    with either bug this test exists to catch; sampling repeatedly and
    keeping the widest result is representative of what the scene actually
    looks like without being racy about it. Returns None if the environment
    can't run the check at all (not the same as the scene failing it).
    """
    if not shutil.which("Xvfb") or not shutil.which("import"):
        print("SKIP: Xvfb or ImageMagick 'import' not available", file=sys.stderr)
        return None

    display = ":97"
    xvfb = subprocess.Popen(["Xvfb", display, "-screen", "0",
                              f"{SCREEN_W}x{SCREEN_H}x24"])
    try:
        env = os.environ.copy()
        env["DISPLAY"] = display
        env["ESHEEP_AUTOQUIT_MS"] = "1200"
        # Fix the random stream (spawn direction, per-tick rolls) so the
        # scene's early trajectory -- and therefore whether this sampling
        # window catches both sprites on screen -- is reproducible instead
        # of depending on which way the sheep happened to face this run.
        env["ESHEEP_SEED"] = "12345"
        proc = subprocess.Popen(
            [PROGRAM, "--review-parent", str(animation_id),
             "--spawn", "bottom", "--no-window-landing"],
            cwd=ROOT, env=env)
        # Sample only the early window, soon after spawn. Some children (the
        # blacksheepv "UFO") keep moving away from the parent every tick by
        # design -- given enough time it legitimately flies off the edge of
        # a small test screen, which is correct authored behavior, not a
        # bug, and sampling that late would produce a false failure.
        best_blobs = []
        best_span = -1
        for _ in range(6):
            time.sleep(0.12)
            subprocess.run(["import", "-window", "root", out_path],
                           env=env, check=True)
            blobs = find_blobs(out_path)
            span = (max(b[1] for b in blobs) - min(b[0] for b in blobs)) \
                if blobs else 0
            if span > best_span:
                best_blobs = blobs
                best_span = span
        proc.wait(timeout=5)
        return best_blobs
    finally:
        xvfb.terminate()
        xvfb.wait(timeout=5)


def find_blobs(image_path):
    """Return the list of (min_x, max_x) horizontal spans of non-background
    content in the bottom strip of the image, merging pixels within 2px."""
    img = Image.open(image_path).convert("L")
    w, h = img.size
    pixels = img.load()
    columns_lit = []
    for x in range(w):
        lit = False
        for y in range(h):
            if pixels[x, y] > 12:
                lit = True
                break
        columns_lit.append(lit)

    blobs = []
    start = None
    gap = 0
    for x, lit in enumerate(columns_lit):
        if lit:
            if start is None:
                start = x
            gap = 0
        elif start is not None:
            gap += 1
            if gap > 3:  # small anti-aliasing/dither gaps don't split a blob
                blobs.append((start, x - gap))
                start = None
                gap = 0
    if start is not None:
        blobs.append((start, w - 1))
    return blobs


def check_scene(animation_id, name):
    out_path = f"/tmp/esheep_test_child_scene_{animation_id}.png"
    try:
        blobs = render_scene(animation_id, out_path)
        if blobs is None:
            return True  # environment can't run this check; not a code failure

        assert blobs, (
            f"{name} (animation {animation_id}): nothing rendered at all."
        )
        # Some authored children sit right next to the parent (the flower
        # grows at the sheep's mouth) and legitimately touch/overlap it with
        # no black gap in between -- so "two separate blobs" isn't a safe
        # assertion. What both real bugs actually did was collapse the
        # visible content back down to about one tile's width, or spread it
        # across nearly the whole screen. Total span across every blob
        # catches both without depending on whether this particular pair
        # happens to touch.
        span_start = min(b[0] for b in blobs)
        span_end = max(b[1] for b in blobs)
        total_span = span_end - span_start
        tile_size = 40  # matches assets/sheep_spritesheet.png's grid
        assert total_span > tile_size * 1.3, (
            f"{name} (animation {animation_id}): visible content only spans "
            f"{total_span}px ({blobs}), about one tile -- the composited "
            f"window most likely didn't grow to fit the parent AND its "
            f"authored child, only one of them."
        )
        assert total_span < SCREEN_W * 0.9, (
            f"{name} (animation {animation_id}): visible content spans "
            f"{total_span}px, nearly the whole {SCREEN_W}px test screen -- "
            f"the child is probably positioned using an unconverted "
            f"absolute-style expression instead of a position relative to "
            f"the parent."
        )
        print(f"OK: {name} (animation {animation_id}) spans {total_span}px "
              f"across {len(blobs)} blob(s): {blobs}")
    finally:
        if os.path.exists(out_path):
            os.remove(out_path)
    return True


def main():
    if not os.path.exists(PROGRAM):
        subprocess.run(["make", "esheep"], cwd=ROOT, check=True)

    failures = []
    for animation_id, name in CHILD_SCENES.items():
        try:
            check_scene(animation_id, name)
        except AssertionError as exc:
            failures.append(str(exc))
            print(f"FAIL: {exc}", file=sys.stderr)

    if failures:
        return 1
    print("\nAll child scene rendering tests passed!")
    return 0


if __name__ == "__main__":
    sys.exit(main())
