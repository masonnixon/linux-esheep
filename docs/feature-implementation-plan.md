# linux-esheep feature implementation plan

## Goal

Implement the complete authored eSheep behavior model and the remaining desktop
features while keeping the current Linux application usable at every phase.
The XML source remains authoritative. Generated C data is checked into the
repository and must remain reproducible.

## Design rules

1. Keep animation data separate from desktop integration.
2. Represent every pet, child, and subchild as an independent runtime actor
   with its own animation state, position, visibility, and parent link.
3. Make authored transitions select the next state. Runtime code supplies
   collision and desktop context rather than replacing the authored graph.
4. Use deterministic random seeds in tests and injectable randomness in the
   runtime.
5. Keep X11 support working while adding other backends.
6. Add a failing test before each behavior change when the failure can be
   reproduced without a live desktop.

## Architecture decisions

- Use one composited scene per sheep actor tree. The scene owns the parent and
  all descendants, which gives stacking, clipping, and input one owner and
  avoids races between separate child windows.
- A child starts when its parent transition creates it and then runs its own
  authored state machine. Destroying or cancelling the parent destroys its
  descendants. Children may create children, subject to a bounded depth and
  cycle checks.
- Convert XML expressions into typed generated operations during generation.
  The validator rejects unsupported expressions; runtime code must not infer
  semantics by matching arbitrary strings.
- Keep actor, transition, expression, and geometry tests in pure C or the
  project’s existing test harness. Use Xvfb only for actual window, input,
  stacking, and geometry integration tests.
- Visual fixtures compare tile IDs, alpha bounds, and relative geometry. They
  must not depend on exact compositor pixels when anti-aliasing can differ.
- Start Wayland support with one documented layer-shell compositor target and
  capability detection. Keep X11 as the complete backend until additional
  Wayland compositors have explicit support.

## Phase 1: Normalize and validate authored data

### Work

- Extend the generator to preserve child records, child expressions, actions,
  and any future authored metadata without special cases.
- Add a generated-data validator for IDs, transition targets, probabilities,
  expression forms, frame ranges, and child-parent relationships.
- Define a stable transition index used by the review tool and tests.
- Document which XML expressions are supported and reject unknown expressions
  with a useful error.

### Acceptance

- Regenerating from XML produces byte-identical C and header files.
- All 54 animations, 67 sequence transitions, 22 border transitions, 4
  gravity transitions, and 3 child records validate.
- The validator fails on malformed IDs, unreachable transition buckets, and
  invalid frame references.

## Phase 2: Build a generic animation actor engine

### Work

- Create an actor structure containing animation state, pose, position,
  direction, parent reference, child list, visibility, and random source.
- Move frame advancement, repeat handling, transition selection, gravity, and
  child creation into the actor engine.
- Support sequence, border, gravity, and child transitions through one authored
  transition path.
- Allow children to create subchildren recursively, with cycle and depth
  protection. A child is independent after creation, and is removed when its
  parent actor is removed or its authored lifetime completes.
- Preserve authored child lifetime rules instead of tying a child to one
  parent frame unless the XML says to do so.

### Acceptance

- Bath, flower, and black-sheep scenes advance through their complete authored
  child graphs.
- Multiple children attached to one actor render independently.
- A child-of-child fixture works in a unit test.
- Every authored transition can be selected by a deterministic test trace.

## Phase 3: Separate rendering from desktop windows

### Work

- Add a renderer that can draw an actor to a transparent surface without
  assuming the actor is the only sprite.
- Use one composited scene per actor tree with explicit stacking and input
  ownership. Do not introduce one desktop window per child.
- Apply authored offsets, opacity, flipping, and pose offsets consistently to
  parents and children.
- Make child windows input-transparent and ensure dragging selects only the
  parent actor.

### Acceptance

- Parent and child positions are measured and asserted in a headless X11 test.
- Child windows remain correctly stacked while moving.
- The eating flower, bath shower, and black-sheep scenes are visible in
  screenshot fixtures.
- No stale child pixels or translucent rectangular backgrounds remain.

## Phase 4: Replace hardcoded behavior with authored context handling

### Work

- Define a runtime context model for floor, window, taskbar, vertical edge,
  horizontal edge, fullscreen, and unsupported surfaces.
- Remove animation-ID-specific transition decisions where authored metadata can
  express the behavior.
- Keep only platform operations in the desktop layer: collision detection,
  monitor selection, window movement, and input.
- Make edge reversal use actual pose direction and authored flip actions.
- Add seeded tests for left and right edge turns, top traversal, falling,
  landing, and respawn.

### Acceptance

- No edge turnaround loop occurs under repeated deterministic traces.
- Every authored border and gravity transition is reachable in runtime tests.
- Position changes agree with the authored pose deltas for every movement
  animation.

## Phase 5: Complete multi-sheep behavior

### Work

- Give each sheep a complete actor tree rather than a separate partial App
  state.
- Add collision policies for sheep-to-sheep overlap, landing, and stacking.
- Define whether children participate in collision detection. Default child
  props should be visual-only.
- Add spawn spacing, monitor assignment, and cleanup for arbitrary counts.

### Acceptance

- Counts from 1 through the configured maximum start without overlap-induced
  deadlocks.
- Sheep remain independent when one is dragged or falls.
- Child props do not steal input or become landing surfaces.

## Phase 6: Finish monitor, X11, and Wayland integration

### Work

- Represent monitor geometry in global coordinates and preserve monitor origin
  for all spawn and child expressions.
- Add explicit monitor transitions and seam policy.
- Improve X11 window classification, stacking, fullscreen handling, and panel
  placement support.
- Add a native Wayland backend behind a capability interface, initially for one
  documented layer-shell compositor target. Retain X11 and XWayland fallback
  paths and report unsupported compositor capabilities clearly.

### Acceptance

- Two-monitor X11 tests cover positive and negative monitor coordinates.
- Sheep can move, land, fall, and drag on either monitor.
- Fullscreen windows suppress or lower the pet according to configuration.
- Wayland behavior is tested on at least one supported compositor, with a
  clear fallback error elsewhere.

## Phase 7: Restore original application features

### Work

- Add a tray icon with show/hide, bring-to-front, pause, quit, and settings
  actions.
- Add an options dialog for pet selection, count, monitor behavior, spawn mode,
  animation review, and sound.
- Add about/help information and current-animation diagnostics.
- Add sound metadata and playback with a no-audio fallback.
- Add autostart and packaging behavior appropriate to Linux desktops.

### Acceptance

- All UI settings persist and have documented precedence.
- Sound failure does not stop animation playback.
- The application can be run twice, paused, hidden, restored, and cleanly
  terminated.

## Phase 8: Runtime pet packages and custom assets

### Work

- Load supported XML behavior definitions at runtime through a validated pet
  package before creating GTK windows.
- Pair each behavior definition with its spritesheet and optional sounds.
- Validate packages before activation and report invalid animation references.
- Add the UFO, pilot, and any future prop animations as authored behavior,
  rather than leaving them as unused sprite tiles.

### Acceptance

- A supported custom XML and matching spritesheet can be selected without
  recompiling the binary.
- Invalid packages fail safely with a readable diagnostic.
- A sample custom pet exercises parent, child, and subchild animations.

## Test strategy

### Unit tests

- XML and generated-data validation.
- Expression evaluation.
- Frame timing and repeat boundaries.
- Transition probability and context filtering.
- Actor child creation, recursion, cleanup, and deterministic random traces.

### Integration tests

- Xvfb parent/child rendering and stacking.
- Window and panel collision geometry.
- Edge reversal and top traversal.
- Multi-sheep input and collision behavior.
- Multi-monitor coordinates.

### Visual review

- Keep the indexed transition viewer.
- Add parent-plus-child playback for every child record.
- Capture reference screenshots for bath, flower, black sheep, falling, edge
  turns, and top traversal.
- Compare only stable geometry and alpha masks where anti-aliasing differs by
  compositor.

## Suggested delivery order

Implement Phases 1 through 4 before adding new desktop UI. They establish a
correct behavior engine and make later platform work testable. Implement Phase
5 next for the requested multi-sheep behavior. Follow with Phase 6 for desktop
coverage, Phase 7 for user-facing original features, and Phase 8 for custom pet
distribution.

Each phase should land as a separate reviewable commit with its own retained
tests and generated-data reproducibility check.

## Explicit non-goals for the first parity release

- Full parity with Windows-only shell APIs or Windows UI behavior is outside
  the Linux backend scope.
- Tray/settings polish, sound, autostart, packaging, and arbitrary runtime XML
  loading follow the core authored behavior and renderer milestones. They are
  not prerequisites for validating the actor engine.
- Supporting every Wayland compositor is not a prerequisite for the first
  native Wayland backend. Each additional compositor needs its own capability
  and integration evidence.

## Handoff execution specification

This section is the source of truth for future implementation handoffs. Each
phase is bounded, independently testable, and reviewable. Agents must read
the repository's `AGENTS.md` instructions, inspect the current worktree, obey
the exact write allowlist, and leave commits to the supervisor unless the
phase explicitly says otherwise. Preserve both planning documents.

### Starting-condition protocol

Before every phase, record the current commit, `git status --short`, and
`git diff --stat`. If another agent is editing overlapping paths, wait for it
to finish or stop it cleanly. Never reset, restore, clean, or overwrite user
changes to establish a baseline. Use a fresh run directory outside the repo.

Every phase must report:

```text
STATUS=PASS|BLOCKED
PHASE=<id>
CHECK_EXIT=<integer>
CHANGED=<comma-separated paths>
BLOCKER=<none or one sentence>
```

### Phase 0: Stabilize falling, landing, and X11

Objective: fix the currently reported regressions before adding features.

Owned paths: `src/main.c`, `src/context.c`, `src/context.h`, focused C tests,
and `Makefile`.

Requirements:

- A sheep released above the floor remains airborne until movement reaches a
  valid surface.
- A falling sheep crosses and lands on ordinary managed windows, including
  reparented Thunar and terminal windows.
- Edge classification cannot override an active fall.
- A missing or stale window does not cause floor teleportation or process exit.
- X11 error handling restores GDK's previous handler and does not terminate on
  `BadWindow`.
- Both monitor edges reverse once and resume inward walking.

Proof:

- Pure tests for fall progression, surface ordering, edge ordering, stale
  objects, and floor fallback.
- Xvfb integration that creates a client, falls onto it, destroys it during a
  refresh, and continues running.
- `make`, `make test`, and `make test-behavior`. If GUI tests cannot run, the
  result must name the environmental limitation.

### Phase 1: Normalize authored data

Objective: make XML, generated C, and runtime metadata agree exactly.

Owned paths: `tools/esheep_animations.xml`, `tools/gen_animations.py`,
`src/animations_data.c`, `src/animations_data.h`, validator tests, and review
tooling.

Requirements:

- Generate typed operations for values, image-relative expressions, random
  expressions, actions, child records, and transition contexts.
- Reject unsupported expressions with source locations and useful diagnostics.
- Validate contiguous IDs, frame bounds, positive probabilities, targets,
  repeat ranges, child references, and bounded child cycles.
- Generate stable transition IDs from source animation, event type, ordinal,
  and target.
- Make the transition viewer consume generated IDs rather than a second map.

Acceptance:

- Regeneration is byte-identical to checked-in generated files.
- Fixtures prove every validator failure path.
- All current animations, transitions, gravity rules, border rules, and child
  records validate.
- Changing XML produces an expected generated diff.

### Phase 2: Complete the pure actor engine

Objective: execute the authored graph independently of GTK or X11.

Owned paths: `src/actor.*`, `src/interpreter.*`, new pure runtime modules,
and actor/interpreter tests.

Requirements:

- Store animation state, frame timing, repeat state, pose, direction,
  visibility, parent, descendants, and per-actor RNG.
- Define event order for frame, sequence, border, gravity, child creation,
  child completion, cancellation, and cleanup.
- Support independent children, multiple children, bounded recursion, cycle
  prevention, detach, and recursive cleanup.
- Preserve authored child lifetime instead of tying a child to one parent frame
  ID.
- Emit typed movement and collision intents to the desktop layer.

Acceptance:

- Seeded traces cover all 54 animations and all authored transition types.
- Flower, shower, black-sheep, multiple-child, and child-of-child fixtures
  complete correctly.
- Large time steps do not silently lose events.
- Existing interpreter and actor tests remain green.

### Phase 3: Finish composited scene rendering

Objective: render complete actor trees without clipping or input corruption.

Owned paths: `src/renderer.*`, GTK draw/input integration, renderer tests,
and visual-review tooling.

Requirements:

- Compute the union of visible alpha bounds for parent and descendants.
- Resize and position the transparent surface around negative child offsets.
- Apply offsets, opacity, flipping, pose offsets, and authored stacking once.
- Keep child props input-transparent while the parent owns dragging.
- Rebuild visible and input regions whenever frame or scene bounds change.
- Do not create one desktop window per child.

Acceptance:

- Geometry tests prove flower placement, shower-over-sheep placement, and
  black-sheep separation.
- Child alignment survives walking, falling, climbing, turning, and dragging.
- Child completion removes stale frames.
- Visual fixtures compare tile IDs, alpha bounds, relative positions, and
  stacking order.

### Phase 4: Build a typed desktop collision backend

Objective: isolate platform geometry and failure handling from animation.

Owned paths: new `src/desktop_backend.*`, `src/context.*`, X11 adapter code,
and desktop integration tests.

Requirements:

- Provide typed operations for monitors, surfaces, traits, stacking, pointer
  coordinates, global positioning, and transient failures.
- Use swept movement collision so a falling step detects surfaces it crosses.
- Select the nearest valid supporting surface below the actor with deterministic
  stacking tie-breaking.
- Replace cached surfaces only after a complete successful refresh.
- Scope X11 error handling, restore the prior handler, and synchronize risky
  requests where necessary.
- Test normal, reparented, unmapped, destroyed, fullscreen, desktop, dock,
  panel, Conky, and override-redirect windows.

Acceptance:

- Xvfb tests cover window creation, movement, destruction, and refresh races.
- A stale-window stress loop produces no unhandled `BadWindow` termination.
- Backend events are typed and consumed by the actor runtime.
- `GDK_SYNCHRONIZE=1` produces no unhandled X errors.

### Phase 5: Integrate the actor and desktop layers

Objective: make GTK a host instead of a second behavior engine.

Requirements:

- Remove animation-ID-specific decisions from `on_tick()` where authored
  metadata can express them.
- Separate grounded, falling, climbing, landing, edge, and dragging states.
- Apply one typed collision event per movement step before dispatching the
  authored transition.
- Separate world direction from authored sprite flipping.
- Give every sheep the same actor and backend lifecycle.

Acceptance:

- Seeded traces cover both directions, edges, top traversal, floor/window
  landing, drag/drop, and monitor changes.
- A 1,000-tick edge trace has no turnaround loop.
- An airborne actor never teleports to floor before a collision event.
- GUI smoke tests run with one and multiple sheep without warnings.

### Phase 6: Complete multi-sheep behavior

Objective: make counts, collisions, input, and cleanup independent.

Requirements:

- Give each root sheep its own actor tree, RNG, desktop state, and input owner.
- Define overlap policy: separate, pass through, land, or stack.
- Do not let collision resolution override a pending authored transition or
  point a sheep into an edge.
- Keep child props visual-only unless explicitly authored as surfaces.
- Handle exhausted spawn locations with a documented fallback.

Acceptance:

- Counts 1, 2, and maximum start without deadlocks.
- Dragging one sheep does not pause or move another.
- Same seeds reproduce traces; different seeds produce independent traces.
- Removing one sheep leaves siblings and descendants valid.

### Phase 7: Harden configuration and custom pets

Objective: make customization safe without recompilation.

Requirements:

- Validate spritesheet dimensions, grid, alpha format, and referenced tiles.
- Define and test defaults, config, environment, and CLI precedence.
- Load validated XML behavior or a validated pet package at runtime.
- Pair behavior data with spritesheet and optional sounds.
- Add UFO, pilot, meteorite, and other assets through behavior metadata.

Acceptance:

- A valid custom package runs without recompilation.
- Invalid XML, expressions, dimensions, and assets fail before partial startup.
- Config precedence and invalid-value tests pass.
- Missing sound support cannot stop animation.

### Phase 8: Multi-monitor and Wayland

Objective: add topology and backend support without regressing X11.

Requirements:

- Preserve global coordinates, negative origins, and explicit monitor ownership.
- Define seam behavior for walking, falling, dragging, and climbing.
- Preserve supporting surfaces through monitor geometry changes.
- Implement a backend capability interface.
- Implement one documented layer-shell Wayland target, retaining X11 and
  XWayland fallback with clear unsupported-capability errors.

Acceptance:

- Two-monitor tests cover unequal heights, negative coordinates, seams,
  dragging, and falling.
- Native Wayland tests cover supported operations and graceful limitations.
- X11 behavior is unchanged when Wayland is unavailable.

### Phase 9: Original desktop features

Objective: close remaining user-facing parity gaps after runtime stability.

Requirements:

- Add tray actions: pause, show/hide, bring-to-front, settings, quit.
- Add settings for pet, package, count, monitor, spawn, landing, walk
  probability, review mode, and sound. Character, spritesheet, package,
  count, monitor, spawn, landing, Conky, tick, walk-probability, and named
  review-animation persistence are implemented; sound controls remain.
- Add about/help and current-animation diagnostics.
- Add isolated sound playback, autostart, install/uninstall checks, and
  version metadata.
- Define behavior for multiple application instances.

Acceptance:

- Settings persist and follow documented precedence.
- Pause/resume preserves parent and child state.
- Hide/show restores actors and input regions.
- Clean quit removes timers, windows, descendants, and temporary resources.
- Packaging tests verify binary, assets, desktop entry, and man page.

### Cross-phase acceptance checklist

Every handoff must include an exact start baseline, write allowlist, denied
paths, retained tests, a deterministic reproducer for bug fixes, and a clear
live-display boundary. The supervisor reviews the diff, reruns the checks,
confirms no dependency was installed on the host, rejects backup files and
unrelated edits, and creates the commit only after all evidence passes.
