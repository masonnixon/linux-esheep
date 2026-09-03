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
  monitor selection, tick interval, window landing, and walking probability.
- One optional status/tray icon with group-wide show/hide, pause/resume,
  bring-to-front, About, and Quit actions.
- Explicit opt-in XDG autostart installation.
- Runtime composition of every authored child record for the active parent,
  with independent frame timing per child slot.
- Pure actor support for querying all authored child records, independent
  child clocks, bounded child trees, and recursive cleanup.
- Sheep-to-sheep overlap resolution and spawn spacing.
- Startup and runtime monitor selection, including negative-coordinate and
  unequal-height monitor seam selection helpers.
- Sheep and replacement penguin spritesheets sharing the same tile grid.
- A transition review tool with indexed playback.
- Unit tests for the pure interpreter, animation data, sprite references, and
  basic runtime startup behavior.
- Generated-data validation covers IDs, transition targets, probabilities,
  supported expressions, frame references, and child relationships, with
  reproducibility checks against the XML source.
- The default test suite stages an installation and verifies the executable,
  sprite assets, desktop files, autostart file, and man page without writing to
  the host installation.

## Partially implemented

### Child animations

The three authored child relationships are present and render in separate
transparent windows:

| Parent | Child | Purpose |
| --- | --- | --- |
| 21 | 23 | Bath or shower effect |
| 26 | 27 | Flower while eating |
| 28 | 31 | Black-sheep companion |

The current renderer supports all authored child records for a parent, with an
independent frame clock per child slot. The platform-independent actor layer
also supports recursively linked child trees and exposes the complete authored
child-record set. The GTK runtime still materializes only authored direct-child
records and does not yet apply child-specific sequence, border, or gravity
transitions. A child is still tied to the lifetime of its immediate parent
animation. This differs from the upstream model, which allows child-created
subchildren and richer child lifetimes.

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

### Validation and packaging

The authored-data validator and deterministic regeneration checks are now
implemented and part of the normal test suite. Packaging layout is also tested
through an isolated `DESTDIR` staging install. These gates do not imply that
the GTK runtime has achieved full authored-graph parity; the remaining runtime
gaps are listed below.

## Missing features

### Animation and child execution

- The standalone actor engine now supports independent child clocks, multiple
  children, bounded child-of-child trees, cycle/depth protection, and subtree
  cleanup. GTK's composed scene also resolves nested authored child records and
  resets child state when the parent animation changes.
- The live GTK path still uses a bounded compatibility representation rather
  than the actor engine as its authoritative runtime tree. Child lifetime and
  authored sequence transitions therefore need one more integration pass.
- Automatic recursive child creation from runtime package metadata is not yet
  complete.
- Child movement, opacity, flipping, and lifetime are partly authored-driven;
  richer action variants and full lifetime semantics remain incomplete.
- Runtime support for every authored expression without special-case parsing.
- Visual regression coverage for all 54 animations and all 96 reviewable
  transitions.

### Desktop integration

- Native Wayland surface and window discovery support.
- Continuous floor traversal across arbitrary monitor layouts. X11 seam
  crossing is implemented for adjacent workareas, but gaps, differing vertical
  origins, and non-rectangular desktop layouts still need policy and
  integration coverage.
- Richer sheep-to-sheep policy beyond grounded stacking, overlap resolution,
  and spawn spacing.
- Complete handling for unusual panel placement and compositor-specific
  stacking. X11 foreground-client occlusion is implemented for normal and
  reparented clients, with live compositor coverage still limited.
- The runtime settings dialog now provides a named chooser for every active
  animation plus normal walking, and persists character, spritesheet,
  package, count, monitor, spawn, landing, Conky, tick, and walk-probability
  settings. Parent-child review remains available through the indexed review
  tool.
- Sound effects and authored sound playback. No audio assets or sound tags
  are present in the current source, and no supported playback library is
  available in the build environment, so this remains a separate integration
  phase rather than a safe local implementation.
- Update and broader application-management behavior from the original
  desktop application.

The upstream history lists fullscreen handling, tray controls, sounds,
multiple children, child subchildren, multiscreen improvements, and animation
options as features added to the original application.

### Pet and asset distribution

- Runtime loading of the supported XML behavior graph via `--package`,
  `ESHEEP_PACKAGE`, or `package=` configuration is implemented.
- Package structure, references, expressions, frame bounds, child graphs, and
  tile grid are validated before window creation.
- Full XML feature parity is still incomplete: authored action variants beyond
  flip, arbitrary expression forms, and richer package metadata are not yet
  represented by the runtime loader.
- Runtime support for authored props that are present in the replacement
  spritesheet but absent from the current behavior graph. For example, the
  penguin sheet contains UFO tiles 158–165 and pilot tiles 166–168, but no
  animation references them. No meteorite animation is currently authored.

## Test coverage gaps

Current tests are strongest for data parsing and interpreter transitions. They
are weak for the parts users see most:

- Pure deterministic tests cover left and right edge reversal, window landing,
  child composition, and child cleanup. They do not replace live compositor
  coverage.
- No test that a sheep lands on a real X11 window at the expected y coordinate.
- No live test that a composed child scene follows its parent while the parent
  moves or that the scene remains correctly stacked.
- No live test that child transitions complete and clean up.
- No live multi-monitor integration test with actual GDK monitor topology.
- Pure coverage now includes grounded sheep stacking. Live simultaneous sheep
  interaction remains limited, while Xvfb verifies foreground-client
  occlusion ordering.
- No frame-by-frame visual comparison against expected screenshots.

## Overall status

The project has a solid authored-data and interpreter base. It is a functional
Linux desktop pet with the core sheep movement loop. It is not yet a complete
runtime of the upstream animation model or a complete replacement for the
original desktop application. The largest technical gap is the child and
runtime state-machine layer, followed by desktop integration and validation.
