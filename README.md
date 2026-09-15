# linux-esheep

A very lightweight Linux recreation of the classic Windows desktop pet
eSheep: a small sprite that walks around your screen, climbs the edges,
falls, and sleeps. Pure C, GTK3, no runtime dependency beyond that.

## Build & run

```sh
make esheep
./esheep
```

Command-line options are available with `./esheep --help`:

```text
--sprite PATH       Use a different spritesheet.
--character sheep    Use sheep or penguin sprites.
--package PATH        Load a validated XML behavior package.
--config PATH         Load settings from an INI config file.
--spawn bottom      Start at the monitor bottom (default).
--spawn window      Start on a visible application window.
--spawn random      Use the authored weighted spawn points.
--count N            Spawn N independent sheep (1-32).
--monitor N          Start on monitor N (zero-based); pointer monitor by default.
--no-window-landing Disable window and panel landing.
--allow-conky       Allow Conky as a landing surface.
--x11-fallback      Use XWayland when available.
--tick-ms N         Set the update interval from 10 to 1000 ms.
--walk-keep N       Set the floor walk keep-walking probability from 0 to 100.
--seed N            Set a reproducible random seed for all sheep.
--review-animation N Show animation N for transition review (0 = normal walk).
--list-animations  List active animation IDs and names.
--list-transitions  List active generated transitions for review tooling.
--list-characters  List the catalog names and availability metadata.
--audio / --no-audio Enable or disable package audio.
--master-volume N  Set package audio volume from 0 to 100.
--max-voices N     Set the package audio voice limit from 1 to 32.
```

Requires `libgtk-3-dev` (`pkg-config gtk+-3.0`). Run from the repo root so
it finds `assets/sheep_spritesheet.png`, or point it elsewhere:

```sh
ESHEEP_SPRITESHEET=/path/to/spritesheet.png ./esheep
```

Use `--character penguin` or `ESHEEP_CHARACTER=penguin` to select the bundled
ice-blue penguin. An explicit `--sprite` path takes precedence.

The optional config file is `$XDG_CONFIG_HOME/esheep/config`, or another path
given with `--config`. It uses an INI section named `[esheep]`:

```ini
[esheep]
character=sheep
spritesheet=/path/to/spritesheet.png
package=/path/to/animations.xml
count=2
monitor=0
spawn=random
tick_ms=33
walk_keep_probability=90
seed=12345
review_animation=0
window_landing=true
exclude_conky=true
audio_enabled=false
master_volume=100
max_voices=8
```

Settings resolve in this order: config file, environment variables, then
command-line options. Each sheep has its own animation, direction, position,
drag state, and GTK window. A normal startup chooses an initial walking
direction randomly; `--spawn random` also randomizes the authored spawn point.

Runtime settings can be overridden with environment variables:

- `ESHEEP_TICK_MS`: update interval in milliseconds, from 10 to 1000.
- `ESHEEP_WINDOW_LANDING=0`: disable X11 window and panel landing.
- `ESHEEP_EXCLUDE_CONKY=0`: allow landing on Conky windows.
- `ESHEEP_X11_FALLBACK=1`: on Wayland with XWayland, select GTK's X11 backend
  so application-window landing remains available.
- `ESHEEP_SPAWN=window`: start on a visible application window instead of
  the default monitor bottom.
- `ESHEEP_COUNT`: number of sheep to spawn, from 1 to 32.
- `ESHEEP_MONITOR`: zero-based startup monitor index; by default the pointer's
  monitor is used.
- `ESHEEP_WALK_KEEP_PROBABILITY`: floor walk probability for repeating the
  walking behavior. The original value is 90. Lower it to see other floor
  behaviors more often.
- `ESHEEP_SEED`: non-zero seed for reproducible, independent random streams.
- `ESHEEP_AUDIO`: enable or disable package audio (`1`/`0`, or true/false).
- `ESHEEP_MASTER_VOLUME`: package audio volume from 0 to 100.
- `ESHEEP_MAX_VOICES`: maximum concurrent package voices from 1 to 32.

Left-click-drag picks the sheep up. Right-click opens a pet menu with
Pause, Hide, Bring to Front, and Quit. Runtime settings can be overridden
with environment variables:

When the desktop provides a legacy status area, one linux-esheep tray icon
offers group-wide show/hide, pause/resume, bring-to-front, About, and Quit
actions, plus a settings dialog for tick rate, walk probability, monitor,
spawn mode, sheep count, review animation, window landing, and Conky filtering.
The sheep
continues to work without a tray area. Character, package, spritesheet, and
sheep-count changes take effect immediately and are saved to the config file.

- `ESHEEP_PAUSED=1`: start with all sheep paused (animation stopped, drag
  still works to pick the sheep up).
- `ESHEEP_HIDDEN=1`: start with all sheep windows hidden (the right-click
  Bring to Front action brings them back).

## Tests

```sh
make test-interpreter
```

Runs the animation-interpreter unit tests (pure C, no GTK/X11 needed).

```sh
make test-runtime
```

Runs the runtime behavior integration tests covering edge reversals, top
traversal, fall animation continuity, landing frames, spawn positions, and
multi-sheep independence (pure C, no GTK/X11 needed).

```sh
make test-assets
```

Checks both sprite sheets against the frame indices `src/animations_data.c`
actually references: every referenced tile is drawn, and tile 174 stays blank.
Needs Pillow.

Run `make test` for all of the above plus the GTK/X11 smoke test. It uses Xvfb
and exits automatically after startup.

For visual review, run `tools/review_transitions.py` after building. It asks the
running binary for the active generated transition table, then plays each
sequence, border, gravity, and child transition back-to-back while printing its
1-based index and source/target in the terminal. Pass an index to review one
transition, for example `tools/review_transitions.py 17 3000`.
For a custom behavior package, pass `--package PATH` and optionally
`--sprite PATH` before the index.

## Install

```sh
make install PREFIX=/usr/local   # or your preferred prefix; DESTDIR also supported
make uninstall PREFIX=/usr/local
```

Installs the binary, bundled spritesheets, behavior XML, catalog inventory,
provenance notices, and a `.desktop` entry. Installation stops with a clear
error if any required tracked file or provenance record is missing.
Use `make install-autostart PREFIX=/usr/local` to additionally enable eSheep
at login; `make uninstall-autostart` removes that opt-in entry.

## What's implemented

- Full behavior graph ported from the original eSheep: 54 animations with
  weighted, context-filtered transitions and child animations (`src/animations_data.c`,
  generated by `tools/gen_animations.py` from `tools/esheep_animations.xml`).
  See [`docs/animations.md`](docs/animations.md) for the grouped catalog and
  how to review any individual animation or multi-sprite scene.
- Animation frame timing, repeat boundaries, gravity transitions, sprite
  flipping, opacity, pose offsets, and authored edge-context transitions are
  applied at runtime.
- Real movement and collision with visible X11 windows, panels, and the
  monitor workarea.
- Fullscreen X11 windows temporarily suppress the sheep on the covered
  monitor.
- Screen-edge and detected-window-side climbing with top-surface traversal.
- Mouse drag and a right-click pet menu with pause, hide, bring-to-front, and quit actions.
- Double-clicking a single sheep exits it; in a multisheep group, double-click
  triggers the authored jump animation.
- Optional start-paused and start-hidden via the `ESHEEP_PAUSED` and `ESHEEP_HIDDEN`
  environment variables.
- Multiple independent sheep with per-sheep movement and drag state.
- Optional INI configuration with environment and CLI overrides.

## Known limitations / not yet built

- Package audio controls are present: `--audio`/`--no-audio`,
  `--master-volume N`, and `--max-voices N`, with matching config keys and
  environment variables. Audio is disabled by default and must be enabled
  explicitly. Package sounds are decoded and played asynchronously
  through `ffplay` when it is available. Missing players and unavailable audio
  remain silent. To smoke-test a real player with an imported package, run
  `ESHEEP_AUDIO_PLAYER=ffplay ./esheep --package path/to/animations.xml`.
  Imported sound-bearing packages are bundled under `assets/Pets` when the
  catalog is installed.
- Native Wayland window discovery and arbitrary popup positioning require
  compositor-specific protocols. On Wayland with XWayland, use
  `--x11-fallback` or `ESHEEP_X11_FALLBACK=1` for the X11 landing backend.
- Sheep use global monitor workareas for movement and landing. Seam crossing
  follows the XRandR monitor geometry, so panel-reserved workarea edges do not
  create a false gap; the Y position is retained when it fits the next
  workarea. Physical gaps and vertically non-overlapping monitors reverse
  inward. Set `ESHEEP_DEBUG_MONITORS=1` to print each live geometry/workarea
  pair for diagnosing compositor or XWayland differences.
- Multiple sheep have independent state, spawn spacing, basic overlap
  resolution, and grounded-sheep stacking. X11 foreground clients can
  occlude a sheep standing on a window behind them; broader compositor-level
  stacking coverage remains future work.
- Custom `--package` XML support covers the authored grammar this runtime
  implements (see `tests/test_animation_data.py`), not full parity with the
  original eSheep format: action variants beyond `flip`, expression forms
  beyond the current arithmetic grammar (`src/expression.c`), and richer
  package metadata are not yet represented by the loader.
- The UFO, pilot, spacecraft, meteorite, and re-entry tiles inherited from the
  original art are covered by the local review-only extension animations. They
  are not all part of the ordinary random walk graph; use
  `--review-animation N` or the catalog renderer to inspect them.


## Custom sprites and characters

The `--sprite PATH` option accepts any valid PNG spritesheet, allowing
third-party characters or re-skinned art to replace the built-in sheep and
penguin. The `--character NAME` option selects a built-in character (`sheep`
or `penguin`) and resolves its default spritesheet. An explicit `--sprite`
path always takes priority.

**Package boundary**: a custom spritesheet uses the same 16×11 tile grid
(176 tiles, 640×440 for sheep, 1280×880 for penguin). A custom behavior
package loaded with `--package` is validated against the runtime expression
grammar and supplies its own grid dimensions; provide a matching
spritesheet with `--sprite` or `spritesheet=`. Use
`tests/test_spritesheet.py` as a reference for frame-coverage rules.

For a compositor-independent visual review, generate the complete animation
catalog with `python3 tools/render_animation_catalog.py --output /tmp/esheep-animation-catalog.png`.
The catalog includes frame IDs and authored child mappings.

Custom characters (any `--character` value other than `sheep` or `penguin`) are
accepted at the CLI and env/config level. A package can be selected with
`--package PATH`, `ESHEEP_PACKAGE`, or `package=` in the config file. Package
loading fails before window creation when the XML graph is malformed or uses
uses invalid expressions.

Exit codes from `esheep` relating to spritesheet loading:
- `1` — spritesheet file could not be read (missing, unreadable, or not a
  valid image).
- `2` — spritesheet loaded but failed validation (wrong dimensions or invalid
  character name).

## Catalog and selection

The frozen upstream inventory contains these names: `bbunny` (Buster Bunny),
`blue_ham_ham` (Blue Ham Ham), `blue_sheep` (gSheep Blue), `esheep64`
(eSheep 64bit), `fox` (fox mate), `green_sheep` (gSheep Green), `grian`
(Grian - Minecraft), `mareep`, `mimiko` (Black Neko Mate), `mumbojumbo`,
`negima`, `neko`, `orange_sheep` (gSheep Orange), `pikachu`, `pingus`,
`pink_fox`, `pink_neko`, `pink_sheep` (gSheep Pink), `purple_sheep`
(gSheep Purple), `red_sheep` (gSheep Red), `shiny_sylveon`, `skeleton`,
`ssj-goku`, `yellow_neko`, `yellow_sheep` (gSheep Yellow), and `zombie`.
Run `./esheep --list-characters` to see aliases and inventory availability.

Only `sheep` and `penguin` are bundled and directly selectable. For example:

```sh
./esheep --character sheep
./esheep --character penguin
./esheep --package /path/to/animations.xml --sprite /path/to/sheet.png
```

The 26 upstream catalog payloads are inventory-only in this release. Their
embedded images, icons, README files, and some sounds are absent from this
checkout, and the inventory records unresolved licenses, so the catalog names
must not be presented as bundled parity. See [`packaging/PROVENANCE.md`](packaging/PROVENANCE.md).

## Planned Wayland migration path

1. Keep the current X11 backend as the complete window-landing implementation.
2. Add a native Wayland surface for bottom and edge walking, dragging, and
   output-scale handling, using layer-shell where the compositor supports it.
3. Add compositor-specific window providers for landing geometry, starting
   with one selected compositor family and keeping the provider optional.
4. Retain XWayland fallback for desktops that do not expose the required
   native protocols.

## Assets

`assets/sheep_spritesheet.png` and the source behavior data in
`tools/esheep_animations.xml` are extracted from the eSheep64 pet bundled
with [Adrianotiger/desktopPet](https://github.com/Adrianotiger/desktopPet)
(sprites originally ripped by LiL_Stenly). See [NOTICE.md](NOTICE.md) --
those assets are not covered by this repo's own code being otherwise
freely modifiable.

`assets/penguin_ice_blue_spritesheet.png` is original artwork for this repo,
generated by `tools/gen_penguin_spritesheet.py` from the drawing rig in
`tools/penguin_rig.py`. It fills the same 16x11 grid so the two characters
share one behavior graph, but is drawn at 2x resolution (80px tiles), so the
penguin renders twice as large on screen as the sheep. See
[assets/penguin_ice_blue_spritesheet.md](assets/penguin_ice_blue_spritesheet.md)
for the tile map and how to regenerate it.
