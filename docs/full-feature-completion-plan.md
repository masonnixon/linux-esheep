# Full character, audio, and runtime completion plan

This plan closes the remaining gaps between linux-esheep and the licensed
upstream Desktop Pet catalog. The user has confirmed that the upstream assets
may be redistributed with this project. Each asset still needs a source URL,
license note, and checksum in the repository provenance record.

## Acceptance target

Every imported character has a loadable package, spritesheet, and authored
audio records where upstream provides them. The settings dialog can switch the
active character and spritesheet during a running session, and can resize the
live pet group. Every selectable option is tested for persistence, runtime
effect, failure handling, and clean shutdown. Existing movement, landing,
occlusion, multi-monitor, animation, and performance tests remain green.

## Phase queue

### ASSET-1 — import licensed upstream catalog assets

Acquire the upstream package XML/images/audio payloads for all catalog entries,
preserve their authored metadata and sound records, add checksums and source /
license attribution, and make the packaging/test inventory reproduce the same
set. Do not change runtime selection code in this phase.

### RUNTIME-1 — live pet profile switching

Introduce an owned runtime profile abstraction containing character, package,
spritesheet, tile grid, and optional sound cache. Reload the profile on the GTK
main thread when settings changes, atomically replace the live group’s shared
assets, and preserve position, count, pause/hidden state, and behavior safety.
Failed profile loads must leave the current pet unchanged.

### UI-1 — complete settings and spritesheet selection

Populate character and spritesheet controls from the verified asset registry,
show availability and useful names, keep custom package paths, and make all
live settings (including count) act immediately. Add GTK-level tests for
selection, persistence, reload, and multi-pet resizing.

### AUDIO-5 — connect imported sounds to live profiles

Validate every imported sound payload through the asynchronous backend, retain
authored ordering/probability/loops, and verify actual playback with a fake
player plus a documented real-player smoke command. Missing audio remains
optional and must never alter movement.

### HARDEN-1 — platform and lifecycle hardening

Complete stale-window error handling, monitor-boundary behavior, true
occlusion, child-scene teardown, and no-display/headless behavior. Add
sanitizers or equivalent lifecycle checks where practical.

### ACCEPT-1 — full parity acceptance

Run the complete build/test/install/provenance/visual suite and representative
live runs for every imported character family, audio-enabled and silent modes,
1/5/10/32 pets, and multi-monitor configurations. Record intentional upstream
differences explicitly.

## Ordering and parallelism

ASSET-1 is first because runtime and UI correctness cannot be validated against
missing payloads. RUNTIME-1 and UI-1 must follow ASSET-1 and touch overlapping
runtime/settings code, so they run sequentially. AUDIO-5 can run alongside
UI-1 after ASSET-1 because it owns audio integration/tests only. HARDEN-1 can
run alongside AUDIO-5 after RUNTIME-1. ACCEPT-1 is last.

The execution policy is:

| Phase | Parallel companions | Gate |
| --- | --- | --- |
| ASSET-1 | None | Starts first and must be accepted before runtime work. |
| RUNTIME-1 | None | Starts after ASSET-1; it owns the shared profile/runtime boundary. |
| UI-1 | AUDIO-5 | Starts after RUNTIME-1; no overlapping files with AUDIO-5. |
| AUDIO-5 | UI-1, HARDEN-1 | Starts after ASSET-1 and uses audio-only ownership. |
| HARDEN-1 | AUDIO-5 | Starts after RUNTIME-1 and owns lifecycle/platform code. |
| ACCEPT-1 | None | Starts only after every implementation phase is accepted. |

When a phase reaches its acceptance gate, launch every newly unblocked phase
whose ownership is disjoint from active phases. Do not serialize disjoint work
waiting for user confirmation, and do not launch phases whose scopes overlap.
Each parallel phase gets its own isolated worktree, run directory, contract,
and acceptance gate; integration into the canonical branch happens only after
the gate passes.

## Handoff profile allocation

Parallel phases must use different Codex profiles. Never run two concurrent
phases through the same profile, even when their file scopes are disjoint.
Prefer distinct free profiles first, recording the selected profile in each
phase capsule and launch prompt. If a profile fails to launch, stalls, or
produces no meaningful result, rotate that phase to another available free
profile and record the reason. Do not rotate merely to exhaust the profile
list, and do not use a paid profile when a functioning free profile is
available. The supervisor still performs the deterministic acceptance gates
and integrates only accepted work.
