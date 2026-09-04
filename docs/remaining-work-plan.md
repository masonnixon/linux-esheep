<!-- Keep the capsule current after every accepted handoff phase. -->

## Continuation Capsule

~~~text
REPO: /home/mason/repos/linux-esheep.git
PLAN: /home/mason/repos/linux-esheep.git/docs/remaining-work-plan.md
HEAD: P1 commit (to be made on top of d884592)
BASELINE: clean
LAST_ACCEPTED: P1 shared X11 desktop snapshot; one snapshot per group per refresh interval
RUN: not started
LAST_GATE: full `make test` equivalent (all 20 targets, including test-x11-refresh,
  test-gui, test-child-scene-rendering, test-cli) run with real X11/Xvfb access
  (a different environment than the one that produced the prior 16/20 note, which
  had no X socket access at all) -- all 20 PASS, including the new
  test_group_snapshot_shares_one_refresh and test_shared_snapshot_consumption
  regressions. Also confirmed by hand on a real multi-monitor desktop with
  `--count 5`: all five sheep visible (this was the original bug report this
  whole plan exists to fix).
NEXT_COMMAND: none -- P1 satisfies the reported bug (`--count 5` invisible sheep).
  P2-P8 are broader code-quality follow-ups, not yet requested; do not start them
  without the user asking.
QWEN_PROMPT: Execute P2 from this plan. Read AGENTS.md. Contract is a hard allowlist. Do only DO/CHECK, preserve KEEP, obey STOP, and end with RESULT only.
SUPERVISOR_PROMPT: Continue this plan using local-model-handoff. Verify HEAD, BASELINE, process state, and LAST_GATE before acting. Accept nothing without deterministic gates; update this capsule before stopping.
~~~

| Phase | Depends on | Mode | State | Observable goal |
|---|---|---|---|---|
| P1 | d884592 | RED -> GREEN | DONE | One X11 desktop snapshot is shared by all sheep in a group per refresh interval. |
| P2 | P1 | GREEN | STUB (not started; not yet requested) | Make collision/fall resolution use one authoritative implementation. |
| P3 | P2 | GREEN | STUB | Reject non-representable custom expression results before integer conversion. |
| P4 | P3 | GREEN | STUB | Free parser scratch state on success and every parse failure. |
| P5 | P4 | PROOF | STUB | Add generated-animation synchronization and drift detection. |
| P6 | P5 | PROOF | STUB | Make visual child-scene checks strict in CI and retain portable skips locally. |
| P7 | P6 | PROOF | STUB | Exercise reparenting, stacking, and stale-window races under a real X11 WM. |
| P8 | P7 | ACCEPT | STUB | Run final build, test, packaging, and documented limitation review. |

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

## P2 — authoritative movement/collision path

Make `esheep_apply_motion()` and `esheep_classify_fall()` agree on window
landing, explicit drop landing, taskbar priority, and floor fallback. Prefer a
small shared helper or a clearly documented API boundary. Add tests for both
window-landing-disabled/drop-enabled and ordinary falling. Remove dead logic
only if public/test call sites are updated.

COMMIT: Unify fall and collision resolution

## P3 — expression range safety

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

## P8 — final acceptance

Run the complete test suite, normal and optimized builds, install staging,
visual catalog generation, CLI help/listing, and a final audit of README/man
documentation against behavior. Record native Wayland and sound as explicit
remaining limitations rather than implying unsupported features are complete.

COMMIT: Finalize remaining feature validation
