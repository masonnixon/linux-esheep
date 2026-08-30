# Ice Blue penguin sprite sheet — coverage notes

`penguin_ice_blue_spritesheet.png` is laid out on the same 16x11 grid as
`sheep_spritesheet.png`, so `esheep_animations[]` frame indices resolve the
same way for either character.

**It is drawn at 2x the sheep's resolution**: 80px tiles, 1280x880, against the
sheep's 40px/640x440. The runtime takes its tile size from `sheet width / 16`
(`src/main.c:652`), so this is supported with no code change — but it also means
the penguin renders twice as tall on screen as the sheep, and its collision box
scales with it. That is deliberate: 40x40 puts a hard ceiling on detail, and the
extra pixels are the only real fix for the art looking coarse.

To go back to 40px, set `TILE = 40` in `tools/penguin_rig.py` and regenerate;
everything else is authored in resolution-independent units.

**Coverage**: 173 of the 176 grid cells are drawn — cell-for-cell parity with
`sheep_spritesheet.png`, so the two characters are drop-in interchangeable.
Only 110 of those are reachable from `esheep_animations[]`; the rest are poses
the original pet's authoring tool could address but this port's behaviour graph
does not. They are drawn anyway, so a future animation edit cannot land on an
empty cell. Tile 174 *is* referenced but is deliberately blank: the sheep sheet
leaves it empty and the flower and burn sequences use it as a "nothing on
screen" beat. `tests/test_spritesheet.py` asserts all of that for both sheets.

## The rig

The character is a **side view facing left**, matching the direction the sheep
sheet is authored in. That direction matters: the runtime never rotates or
re-poses a tile, it only mirrors the whole sprite horizontally when the
character walks the other way (`sprite_is_flipped` in `src/main.c`). A
front-facing sprite would read as walking sideways-on in both directions, which
is what the first cut of this sheet got wrong.

Art is authored in a fixed 40-unit coordinate space (`UNITS` in
`tools/penguin_rig.py`) that is independent of output resolution, so changing
`TILE` rescales every pose without touching a single coordinate.

`tools/penguin_rig.py` holds the drawing rig — four views (side, three-quarter,
front, rear), parameterised by lean, crouch, squash, head offset and angle, beak
opening, flipper angles, leg phase and eye state (open, half, closed, sad,
shock, X, spiral). Art is drawn 8x oversampled and point-sampled back down, so
edges stay crisply pixelated rather than anti-aliased — the sheet has zero
partial-alpha pixels.

Line weight is deliberately heavy: a pure-black outline 1.7 units thick. An
earlier cut used a thin outline in a near-body colour, which read as blurred
even though nothing was actually resampled. The body value was lifted to
`(48, 72, 112)` for the same reason — the original `(34, 50, 78)` was so close
to the outline that internal forms disappeared.

`shade()` then models the flat fills into volume: a form shadow toward the lower
right and a rim light toward the upper left, stacked from translucent ellipses
and clipped to the sprite's own silhouette. It composites before the downsample,
so it lands as a handful of discrete tones rather than a smooth ramp. This is
what closes the last of the quality gap — the sheep averages 12.9 tones per
tile with its largest flat area at 30%; without shading the penguin sat at 7.9
and 40%, with it at 13.6 and 29%.

`tools/gen_penguin_spritesheet.py` maps that rig onto the 173 tiles. Regenerate
with:

    python3 tools/gen_penguin_spritesheet.py

It is idempotent and rebuilds the whole sheet, plus the
`penguin_sheet_preview.png` contact sheet, from scratch.

The base "Glossy Classic / Ice Blue" design it recolors is variant G of
`tools/gen_penguin_cartoon_variants.py`, which is kept as the record of the
design exploration; it does not feed the sheet build.

## How the tiles are grouped

- **Locomotion** (2, 3, 6 walk/idle; 4, 5 run) and the turn-around (3 → 9
  three-quarter → 10 front).
- **Drowsing and sleeping** (7, 8, 0, 1 side; 31–33) — the head sinks and the
  beak swings down progressively.
- **Peering over a ledge** (37, 107, 108, 38, 39, 77–80) and **eating**
  (58–61), same mechanism at deeper angles.
- **Reactions**: spiral-eye daze (50, 51, 48), X eyes (47), sad (119), sleepy
  (81, 82), flailing fall (46), sweat (133, 134), retching (127–130), and the
  flat-on-its-back death pose (96).
- **Rear views** (12, 13, 49, 103–106) and the dragged-by-the-mouse frames
  (42–44), which hang the character by its scruff with its legs kicking.
- **Walls and ceilings** (15–21, 23–25, 28–30, 40, 41, 45, 97, 98). These are
  authored *pre-rotated*, the same trick the sheep sheet uses, since the engine
  will not rotate a tile at runtime.
- **Boing** (62–70): one tucked-into-a-ball pose spun through eight 45-degree
  steps, plus a flattened impact frame. The ball is drawn near-circular
  specifically so rotating it does not clip the tile.
- **Catching fire** (135–145): a wobbly three-ring flame envelope composited
  *behind* the body with a few licks in front, over a body that darkens to soot
  across the sequence.
- **Face closeups** (169–171): a band across the top 45% of the tile, eyes
  open / half / closed.
- **Big-face reaction shots** (34–36, 81–83, 89, 90, 101, 102, 115, 119, 123):
  a head filling the whole tile with just the toes showing. The rotated copies
  (85, 91, 94, 95, 112–118, 120–126) spin that head freely rather than in
  90-degree steps — it is near-circular, so any angle stays inside the tile.
- **Odds and ends the behaviour graph never reaches**: the necktie gag (54, 55),
  crying (110, 111), a blank shocked stare (99, 100), nibbling something red off
  the floor (52, 53) and assorted grazing and idle beats.
- **The black-penguin cameo** (154–157): a grey silhouette on an opaque black
  tile, mirrored to face the other way — the same gag as the sheep's.
- **Props**: the bath and shower (146–148) and the flower growth stages
  (149–153) are drawn natively. The flowers matter because `flower` is a real
  animation, so upscaled 40px art would sit visibly chunkier than everything
  around it. The UFO (158–165), its pilot (166–168) and the grid overlay (172)
  are still copied from `sheep_spritesheet.png` and upscaled — none of them is
  the character, and no animation reaches them.

## Known limits

- The rear views are, by nature, a featureless dark blob — a penguin's back
  genuinely has nothing on it. They read fine in motion but carry no expression.
- The retching frames (127–130) render the stream as a short dashed line;
  there is not much room for anything more literal, which is probably for the
  best.
- The copied UFO, pilot and grid tiles (158–168, 172) are 40px sheep art scaled
  up, so their pixels are twice the size of everything drawn natively. Nothing
  in the behaviour graph displays them, so this is invisible in practice — but
  it would need redrawing if an animation ever used them.
