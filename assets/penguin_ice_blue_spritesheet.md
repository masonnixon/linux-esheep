# Ice Blue penguin sprite sheet — coverage notes

`penguin_ice_blue_spritesheet.png` is laid out on the same 16x11 grid (40px
tiles, 640x440) as `sheep_spritesheet.png`, so `esheep_animations[]` frame
indices resolve the same way for either character.

Only the animations reachable from `walk` (id 1) via `none`/`vertical`
transitions and gravity are drawn — i.e. the Phase 2 "core engine" scope
(screen-edge collision, no window/taskbar docking yet). That's animations
1, 2, 3, 5, 6, 9, 10, 15, 16, 37-42 (walk, spin, the fall variants, sleep,
and vertical/top-edge walking), covering 36 distinct tile indices:

```
0, 1, 2, 3, 6, 9, 10, 12, 13, 15, 16, 17, 19, 20, 24, 28, 30, 31, 32, 33,
37, 38, 39, 46, 47, 48, 49, 77, 78, 79, 80, 97, 98, 107, 108, 133
```

Notes on fidelity:

- Walk (2, 3), spin (9, 10), fall (46-49, 133), and sleep (0, 1, 31-33,
  77-80, 107-108) are distinct, purpose-drawn poses.
- Vertical/top-edge walking (animations 37-42, tiles 15/16/17/19/20/24/28/
  30/37/38/39/97/98) currently reuse the plain stand pose as a placeholder
  — a real side-on climbing rig hasn't been drawn yet.
- All other 140 tiles (window/taskbar docking, eating, sneezing, spawning,
  etc. — animations outside the Phase 2 BFS) are transparent/blank. They'll
  need real frames whenever that animation range is brought into scope.

Regenerate with the scratchpad script used to build this (procedural PIL
draw, not hand-pixeled) if the base "Ice Blue" design changes.
