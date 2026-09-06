<!-- Keep the capsule current after every accepted handoff phase. -->

## Continuation Capsule

~~~text
REPO: /home/mason/repos/linux-esheep.git
PLAN: /home/mason/repos/linux-esheep.git/docs/remaining-work-plan.md
HEAD: 9d79436
BASELINE: docs/remaining-work-plan.md intentionally dirty (one capsule change)
LAST_ACCEPTED: repository audit; d884592; full make test passed
LAST_ACCEPTED: P1R shared X11 restacking cache repair; 767c180; full make test passed
LAST_ACCEPTED: P2 authoritative movement/collision path; 09e4f00; full make test passed
LAST_ACCEPTED: P6 strict visual-test mode; 7fe2d3e; targeted strict test and make test passed
LAST_ACCEPTED: P3 expression range safety; 16fc3ed; targeted and full test gates passed after stale-test repair
LAST_ACCEPTED: P4 parser cleanup; 995876d; targeted parser/package gate passed
LAST_ACCEPTED: P5 generated-data synchronization; 633f6b7; sync and animation-data gates passed
LAST_ACCEPTED: PERF multisheep scheduling/rendering; 67cdd26; scaling and full test gates passed
LAST_ACCEPTED: ANIM original animation and sprite-tile parity; e6e48de; parity, tile, child, catalog, and generation gates passed
LAST_ACCEPTED: MONITOR seam crossing and workarea correctness; a187e26 plus repair 7ab474f; focused monitor/runtime gates passed
LAST_ACCEPTED: TRANSITION new transition visual correctness; 50a4a1a; parity, animation-data, child, spritesheet, catalog, and full test gates passed
LAST_ACCEPTED: P7 real X11 window-manager integration; 9d79436; focused 7/7 integration gate, child-scene rendering, parity, and full make test passed
LAST_ACCEPTED: P8 final acceptance; pending final validation commit; complete make test, optimized build, staged install, CLI, catalog, and real-WM gates passed
ACTIVE: none
LAST_GATE: P7 PASS; real reparenting WM geometry, occlusion, stale-window, error, snapshot, and cached-restack coverage validated
NEXT_COMMAND: execute P8 acceptance gates directly
SUPERVISOR_PROMPT: Continue this plan using local-model-handoff. Verify HEAD, BASELINE, process state, and LAST_GATE before acting. Accept nothing without deterministic gates; update this capsule before stopping.
~~~

| Phase | Depends on | Mode | State | Observable goal |
|---|---|---|---|---|
| P1 | d884592 | RED -> GREEN | ACCEPTED_WITH_REPAIR | One X11 desktop snapshot is shared by all sheep in a group per refresh interval. |
| P1R | P1 | GREEN | ACCEPTED | Per-sheep snapshot consumption performs no X11 discovery queries; only the minimal restack request remains. |
| P2 | P1R | GREEN | ACCEPTED | Make collision/fall resolution use one authoritative implementation. |
| P3 | P2 | GREEN | ACCEPTED | Reject non-representable custom expression results before integer conversion. |
| P4 | P1R | GREEN | ACCEPTED | Free parser scratch state on success and every parse failure. |
| P5 | P1R | PROOF | ACCEPTED | Add generated-animation synchronization and drift detection. |
| P6 | P1R | PROOF | ACCEPTED | Make visual child-scene checks strict in CI and retain portable skips locally. |
| P7 | P1R, MONITOR, TRANSITION | PROOF | ACCEPTED | Exercise reparenting, stacking, and stale-window races under a real X11 WM. |
| PERF | P1R | GREEN | ACCEPTED | Reduce per-sheep scheduling and rendering overhead; prove scaling at 1, 5, and 10 sheep. |
| ANIM | P5 | RED -> GREEN | ACCEPTED | Reach original animation and sprite-tile parity, including missing spacecraft, re-entry, meteorite, and child scenes. |
| MONITOR | P1R | RED -> GREEN | ACCEPTED | Diagnose and fix real multimonitor seam crossing and workarea mismatches. |
| TRANSITION | ANIM | RED -> GREEN | ACCEPTED | Verify and correct new animation transitions and spacecraft child composition. |
| P8 | P2-P7, PERF, ANIM, MONITOR, TRANSITION | ACCEPT | ACCEPTED | Final build, test, packaging, CLI, catalog, and documented limitation review passed. Native Wayland positioning and sound remain explicit limitations. |

## Scope and invariants

The implementation remains GTK3/X11-first. GTK objects and animation state stay
on the GTK main thread. The optimization is shared observation, not unsafe GTK
threading: desktop discovery should happen once for the group and its immutable
snapshot should be consumed by every sheep.

Every phase makes one focused commit. Do not install host dependencies, alter
assets without an explicit phase, use destructive Git commands, or silently
broaden native Wayland support. Preserve current edge reversal, falling,
window landing, desktop filtering, multi-sheep, child composition, and X11
error handling behavior.

## P1 — shared X11 desktop snapshot

MODE: RED -> GREEN
START_HEAD: d884592
OWN: src/main.c, tests/test_x11_refresh.c, tests/test_multisheep.c, this document
DENY: assets/, tools/esheep_animations.xml, generated animation tables,
      packaging/, unrelated documentation, native Wayland implementation
KEEP: full existing test suite; per-sheep animation and drag state; current
      X11 frame/reparenting and desktop-surface filtering semantics

DO:

1. Add a deterministic regression seam that demonstrates five sheep do not
   independently rescan the same X11 client list during one refresh interval.
   The assertion must count snapshot refreshes, not merely assert that five
   sheep eventually see equivalent data.
2. Introduce group-owned snapshot storage containing the complete surface list,
   stacking data, fullscreen state, and validity/error state needed by current
   landing and occlusion behavior.
3. Refresh that snapshot once per group interval, then make each sheep consume
   it without issuing its own X11 property/tree/geometry scan.
4. Preserve refresh-on-drag-release and refresh behavior when windows disappear;
   do not make the snapshot stale indefinitely.
5. Keep X11 error handling scoped and safe. No background thread may call GTK.
6. Add a short comment explaining snapshot ownership and invalidation rules.

CHECK:

- First add/run the RED regression against the baseline and record that it fails
  for the missing shared-refresh invariant.
- Run the targeted X11 and multisheep tests.
- Run `make test` with output captured outside the repository.
- Verify `git diff --check`, exactly one requested commit, and a clean tree.

COMMIT: Share one X11 desktop snapshot across sheep
STOP: one commit; no install; no reset/restore/clean; no unrelated cleanup

## P1R — remove per-sheep X11 restacking discovery

MODE: GREEN
START_HEAD: 348eed1
OWN: src/main.c, tests/test_x11_refresh.c, tests/test_multisheep.c
DENY: assets/, tools/esheep_animations.xml, generated animation tables,
      packaging/, documentation, unrelated behavior changes
KEEP: P1 shared snapshot behavior; all existing tests; true foreground
      occlusion; reparented-window safety; desktop-surface exclusion

DO:

1. Extend the shared snapshot with the validated root-level stack window,
   visibility, geometry, desktop classification, and stacking information
   needed by restacking. Populate those values during the one group scan.
2. Refactor the restacking path so snapshot consumers do not call
   XQueryTree, XGetWindowAttributes, XTranslateCoordinates, or property
   queries per sheep. Per-sheep work may select an overlapping cached target
   and issue only the guarded ConfigureWindow request for that sheep.
3. Preserve the standalone test path and stale-window BadWindow/BadMatch
   protection. If a cached target disappears, safely skip it and let the next
   group scan repair the snapshot.
4. Add an anti-false-positive test that counts discovery queries or otherwise
   proves a five-sheep consume cycle does not rediscover window geometry.

CHECK:

- Run targeted X11 and multisheep tests.
- Run `make test` with output captured outside the repository.
- Verify no per-sheep discovery calls remain in the shared consume path.
- Verify exactly one commit, allowed paths only, and a clean tree.

COMMIT: Cache X11 restacking geometry per snapshot
STOP: one commit; no install; no reset/restore/clean; no unrelated cleanup

## P2 — authoritative movement/collision path

Make `esheep_apply_motion()` and `esheep_classify_fall()` agree on window
landing, explicit drop landing, taskbar priority, and floor fallback. Prefer a
small shared helper or a clearly documented API boundary. Add tests for both
window-landing-disabled/drop-enabled and ordinary falling. Remove dead logic
only if public/test call sites are updated.

COMMIT: Unify fall and collision resolution

## P3 — expression range safety

MODE: GREEN
START_HEAD: 09e4f00
OWN: src/main.c, src/interpreter.c, src/expression.c, src/pet_package.c,
     tests/test_expression.c, tests/test_pet_package.c,
     tests/test_child_animations.py (stale assertion repair)
DENY: assets/, tools/esheep_animations.xml, generated animation tables,
      packaging/, unrelated documentation, movement/X11 behavior
KEEP: valid authored expressions, negative values where grammar permits them,
      current package error reporting and all existing tests

Validate finite numeric results against the target integer range before every
conversion used for positions, repeats, frame indices, or `Convert(...Int32)`.
Reject invalid package input with a useful error; retain valid negative values
where the authored grammar permits them. Add boundary and overflow tests.

COMMIT: Validate package expression integer ranges

## P4 — parser cleanup

Make package loading release `ParseState.text` and any outstanding
`PendingNext` allocations on both successful completion and all parser-error
paths. Initialize `*out` defensively when possible. Add malformed XML tests
that exercise an unfinished transition and confirm no sanitizer-visible leak
if a sanitizer gate is available.

COMMIT: Clean up package parser state

## P5 — generated data synchronization

Add a documented, deterministic generation target and a test/CI check that
regenerating from `tools/esheep_animations.xml` produces no diff. Keep generated
files committed and avoid requiring Python dependencies beyond the standard
library for this check.

COMMIT: Verify generated animation data stays synchronized

## P6 — strict visual-test mode

Keep local environments able to skip the ImageMagick/Xvfb visual test, but add
an explicit strict mode for CI and make the project test target use it when
requested. Ensure missing prerequisites are reported as skipped, never as an
unexplained pass.

COMMIT: Add strict child-scene visual test mode

## P7 — real X11 WM integration

Add an opt-in integration harness using an available nested X server and a
lightweight reparenting WM. Cover actual frame geometry, foreground occlusion,
window destruction during refresh, and BadWindow/BadMatch handling. Skip only
when the explicitly documented external WM prerequisite is absent; never
change application behavior merely to satisfy the fixture.

COMMIT: Add reparenting WM integration coverage

## PERF — multisheep scheduling and rendering performance

MODE: GREEN
START_HEAD: 16fc3ed
OWN: src/main.c, src/actor.c, src/renderer.c, tests/test_multisheep.c,
     tests/test_performance.py, Makefile (only performance-test wiring)
DENY: assets/, tools/esheep_animations.xml, generated animation tables,
      packaging/, X11 landing semantics, animation behavior changes unrelated
      to measured overhead, broad documentation changes
KEEP: GTK main-thread ownership, shared X11 snapshot semantics, true occlusion,
      drag behavior, animation frame progression, and all existing tests

DO:

1. Measure current CPU/frame behavior for 1, 5, and 10 sheep using a
   deterministic headless or instrumented harness; record the baseline outside
   the repository.
2. Reduce avoidable per-sheep scheduling overhead. Prefer one group timer and a
   bounded group tick that updates each sheep on the GTK main thread, while
   preserving per-sheep state and configured tick timing.
3. Batch or suppress duplicate child-scene, window synchronization, input-shape,
   and draw work when state has not changed. Do not introduce GTK calls from a
   worker thread.
4. Add an anti-false-positive performance regression seam that counts group
   ticks, draw requests, or equivalent measurable work for 1/5/10 sheep and
   proves work does not multiply unnecessarily.
5. Preserve the existing shared X11 snapshot and restacking behavior.

CHECK:

- Run the benchmark/regression harness at 1, 5, and 10 sheep and report the
  measured counters or timing outside the repository.
- Run targeted multisheep/performance tests and `make test`.
- Run `git diff --check`; verify no GTK calls occur off the main thread; make
  exactly one commit with only the allowed paths.

COMMIT: Batch multisheep scheduling and rendering work
STOP: one commit; no install; no reset/restore/clean; no unrelated cleanup

## ANIM — original animation and sprite-tile parity

MODE: RED -> GREEN
START_HEAD: resolve after P5 is accepted
OWN: assets/, tools/esheep_animations.xml, tools/gen_animations.py,
     src/animations_data.c, src/animations_data.h, tests/test_animation_data.py,
     tests/test_spritesheet.py, tests/test_child_animations.py, docs/animations.md
DENY: GTK/X11 runtime behavior, parser/expression logic, multisheep scheduling,
      packaging, unrelated artwork, and undocumented substitutions
KEEP: the current 54 authored animations, existing frame indices and tile map,
      valid child-scene placement, generated-data synchronization, and all
      existing tests

DO:

1. Establish a source-of-truth inventory from the original eSheep animation
   package/source and the shipped sprite sheets: every tile, animation ID/name,
   frame sequence, transition, pose, and child composition must be accounted for
   as implemented, intentionally unused, or unavailable with evidence.
2. Add RED tests that fail for each confirmed missing original animation or tile
   mapping, including spacecraft, atmospheric re-entry/flight, meteorite/comet, and any
   multi-sprite scenes present in the original source. Every non-padding sprite tile
   must be reachable through at least one reviewable animation or child scene.
3. Implement the missing authored records and child scenes in the XML/source
   package, preserving original frame order, timing, transition context,
   directionality, offsets, and probabilities. For tiles present in the shipped
   art but absent from the recovered original graph, author the smallest
   deterministic reviewable animation that uses the actual tile sequence and
   document it as an art-completeness extension rather than silently leaving it
   unreachable.
4. Regenerate committed animation data deterministically and update the catalog
   and parity tests. Ensure every referenced tile is within the sheet and every
   original tile has an explicit parity status.
5. Exercise every animation and child composition through the review harness,
   including intermediate frames rather than only one frame per scene.

CHECK:

- Run the inventory/parity tests and prove the RED cases turn GREEN.
- Run generation synchronization, spritesheet, child-animation, and visual
  catalog checks, followed by `make test`.
- Review the complete animation catalog and confirm every non-padding tile is
  reachable, including the spacecraft, re-entry, meteorite, and comet effects.
  Only transparent padding may remain intentionally unreachable.
- Verify `git diff --check`, allowed paths only, and exactly one commit.

COMMIT: Implement original animation and sprite-tile parity
STOP: one commit; no install; no reset/restore/clean; no unrelated cleanup

## MONITOR — multimonitor seam crossing and workarea correctness

MODE: RED -> GREEN
START_HEAD: resolve after the preceding accepted phase
OWN: src/main.c, tests/test_multisheep.c, tests/test_runtime.c, README.md
DENY: assets/, animation XML/generated data, parser/expression logic,
      multisheep scheduling, packaging, unrelated X11 occlusion behavior
KEEP: startup monitor selection, global coordinates, monitor workareas,
      edge reversal when no adjacent monitor exists, floor/window landing,
      single-monitor behavior, and GTK main-thread ownership

DO:

1. Capture the live GTK workareas and XRandR monitor geometry for the reported
   four-monitor horizontal chain. Compare exact origins, widths, heights,
   panel-reserved workareas, and coordinate signs; add a debug/fixture seam
   that makes mismatches observable without requiring a physical monitor.
2. Reproduce crossing at the left and right edges for equal-height adjacent
   monitors, including the DP-1 -> DP-3 -> HDMI-1-0 -> DP-5 arrangement.
   Reproduce non-crossing for gaps and vertically non-overlapping monitors.
3. Fix the smallest underlying cause so a sheep crossing a valid seam changes
   to the adjacent monitor exactly once, preserves its Y position when valid,
   continues in the same direction, and cannot immediately reverse or loop.
   Do not use a broad desktop bounding rectangle as a substitute for monitor
   topology.
4. Add deterministic regressions for GTK/XRandR workarea mismatch, seam
   crossing in both directions, gaps, vertical offsets, negative monitor
   coordinates, and repeated ticks at a boundary.
5. Document the actual crossing behavior and any compositor/Wayland limits.

CHECK:

- Run focused monitor and multisheep tests, including the reproduced topology.
- Run `make test` and an opt-in live X11 check when the display supports it.
- Verify no boundary loop, teleport, or duplicate monitor transition occurs;
  run `git diff --check` and make exactly one commit.

COMMIT: Fix multimonitor seam crossing
STOP: one commit; no install; no reset/restore/clean; no unrelated cleanup

## TRANSITION — new transition visual correctness

MODE: RED -> GREEN
START_HEAD: 7ab474f
OWN: tools/esheep_animations.xml, src/animations_data.c, src/animations_data.h,
     tests/test_transition_parity.py, tests/test_child_animations.py,
     tests/test_child_scene_rendering.py, docs/animations.md
DENY: original animation records 1–54 unless a test-only compatibility fix is
      required, runtime C behavior, parser/expression logic, multisheep,
      monitor/X11 behavior, assets, packaging, and unrelated documentation
KEEP: all 110 transition mappings, generated-data synchronization, every
      original animation, all existing parity tests, and explicit tile coverage

DO:

1. Review every new transition 94–110 against its authored frame sequence,
   source/target, timing, and rendered catalog/contact sheet. Treat the first
   93 transitions as a retained baseline and report any mismatch rather than
   rewriting them.
2. Verify the new sequences render in their intended direction and frame order:
   face/hand/glasses/wave, tumble/recovery, meteorite, comet, falling body,
   atmospheric re-entry, spacecraft flight, pilot, and grid art.
3. Verify transition 110's child composition using the actual tile artwork and
   renderer coordinate semantics. Correct its x/y placement or remove the
   artificial child relation if the tiles are standalone; do not copy offsets
   from an unrelated black-sheep scene.
4. Add deterministic tests for each new animation's exact frames and each new
   transition's exact mapping, plus a child-scene placement assertion for 110.
5. Regenerate data, run the parity, child, spritesheet, catalog, and full tests.

CHECK:

- `make test-transition-parity`, animation-data, spritesheet, child, and visual
  catalog checks pass.
- Review the new catalog rows and transition 94–110 output; no new transition
  is silently skipped or rendered as a blank/incorrectly placed scene.
- Verify `git diff --check`, allowed paths only, and exactly one commit.

COMMIT: Correct new transition compositions
STOP: one commit; no install; no reset/restore/clean; no unrelated cleanup

## P8 — final acceptance

Run the complete test suite, normal and optimized builds, install staging,
visual catalog generation, CLI help/listing, and a final audit of README/man
documentation against behavior. Record native Wayland and sound as explicit
remaining limitations rather than implying unsupported features are complete.

COMMIT: Finalize remaining feature validation
