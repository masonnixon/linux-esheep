# linux-esheep feature assessment

## Scope

This assessment compares the current Linux implementation with the authored
behavior source in `tools/esheep_animations.xml` and the feature history of the
upstream eSheep project:

- [Upstream project](https://github.com/Adrianotiger/desktopPet)
- [Upstream changelog](https://adrianotiger.github.io/desktopPet/Changelog.html)

The authored XML contains 54 animations, 67 sequence transitions, 22 border
transitions, 4 gravity transitions, and 3 child-animation records.

## Implemented

The current implementation has these working foundations:

- Generated C animation data derived from the authored XML.
- Frame timing, frame sequences, repeat counts, repeat-from positions,
  sequence transitions, border transitions, and gravity transitions.
- Authored sprite flipping, opacity, vertical offsets, and pose movement for
  the supported expression forms.
- Walking, running, falling, sleeping, peeing, dragging, jumping, edge
  climbing, and top-surface traversal.
- X11 detection of visible application windows, panels, and taskbars.
- Falling onto detected windows and walking on supported surfaces.
- Conky exclusion by default, with an option to allow it.
- Multiple independent sheep at startup.
- Configurable spritesheet, character selection, spawn mode, sheep count,
  tick interval, window landing, and walking probability.
- Sheep and replacement penguin spritesheets sharing the same tile grid.
- A transition review tool with indexed playback.
- Unit tests for the pure interpreter, animation data, sprite references, and
  basic runtime startup behavior.

## Partially implemented

### Child animations

The three authored child relationships are present and render in separate
transparent windows:

| Parent | Child | Purpose |
| --- | --- | --- |
| 21 | 23 | Bath or shower effect |
| 26 | 27 | Flower while eating |
| 28 | 31 | Black-sheep companion |

The current renderer supports one child per parent. Child state is advanced by
its frame timing, but child sequence, border, gravity, and child-of-child
transitions are not interpreted. A child is also tied to the lifetime of its
immediate parent animation. This differs from the upstream model, which allows
multiple children and child-created subchildren.

Child placement and stacking have received targeted fixes, but there is no
automated screenshot or geometry test proving that every child remains beside
or above its parent throughout its animation.

### Runtime behavior graph

The pure interpreter can exercise every generated transition. The GTK runtime
still contains hardcoded cases for walking, falling, climbing, landing, and
edge handling. Those cases can bypass or reinterpret authored transitions.
The existing runtime tests do not model real screen coordinates, detected
windows, edge collisions, or complete child lifecycles.

### Random behavior

Startup direction, authored spawn selection, repeat expressions, and walking
probability have random inputs. The implementation has support for the current
expression vocabulary, but the behavior is not yet verified statistically or
against seeded reference traces.

### Sprite coverage

The asset test verifies that referenced animation frames exist in both sheets.
It does not verify visual alignment, child composition, transparency masks,
animation readability, or that every authored frame is displayed in its
correct runtime context.

## Missing features

### Animation and child execution

- Generic child state machines with their own sequence transitions.
- Multiple children attached to one parent.
- Recursive child creation.
- Child movement, opacity, flipping, and lifetime driven entirely by authored
  data.
- Runtime support for every authored expression without special-case parsing.
- Visual regression coverage for all 54 animations and all 96 reviewable
  transitions.

### Desktop integration

- Native Wayland surface and window-discovery support.
- Seamless movement across monitor boundaries. Sheep currently use one
  monitor workarea at a time.
- Sheep-to-sheep collision and landing.
- Fullscreen application detection and temporary backgrounding.
- Complete handling for unusual panel placement and compositor stacking.
- A proper tray icon, settings/options UI, about view, and animation chooser.
- Sound effects and authored sound playback.
- Autostart, update, and application-management behavior from the original
  desktop application.

The upstream history lists fullscreen handling, tray controls, sounds,
multiple children, child subchildren, multiscreen improvements, and animation
options as features added to the original application.

### Pet and asset distribution

- Runtime loading of a complete XML pet definition. The current program
  compiles the behavior graph into C and only swaps the spritesheet through
  configuration.
- Validation of custom XML files before use.
- Runtime support for authored props that are present in the replacement
  spritesheet but absent from the current behavior graph. For example, the
  penguin sheet contains UFO tiles 158–165 and pilot tiles 166–168, but no
  animation references them. No meteorite animation is currently authored.

## Test coverage gaps

Current tests are strongest for data parsing and interpreter transitions. They
are weak for the parts users see most:

- No deterministic GUI test for left and right edge reversal.
- No test that a sheep lands on a real X11 window at the expected y coordinate.
- No test that a child window follows its parent while the parent moves.
- No test that child windows stack correctly.
- No test that child transitions complete and clean up.
- No multi-monitor integration test.
- No test for simultaneous sheep interactions.
- No frame-by-frame visual comparison against expected screenshots.

## Overall status

The project has a solid authored-data and interpreter base. It is a functional
Linux desktop pet with the core sheep movement loop. It is not yet a complete
runtime of the upstream animation model or a complete replacement for the
original desktop application. The largest technical gap is the child and
runtime state-machine layer, followed by desktop integration and validation.
