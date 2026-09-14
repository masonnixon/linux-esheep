# HARDEN-1: Platform and lifecycle hardening

PHASE: HARDEN-1 — harden X11, monitor, occlusion, and shutdown behavior
MODE: GREEN
START_HEAD: bbd1a79
OWN: `src/main.c`, `src/context.c`, `src/actor.c`, platform/lifecycle tests,
and focused platform documentation.
DENY: assets, catalog/package parsing, settings UI, audio implementation,
animation tables, and unrelated cleanup.

## DO

1. Treat stale or destroyed X11 window IDs as ordinary transient state. Avoid
   asynchronous `BadWindow`/`BadMatch` crashes during discovery, stacking, and
   geometry queries; refresh or discard invalid targets safely.
2. Preserve correct falling and landing across monitor work areas and monitor
   boundaries, including drag-release without snapping to a screen edge.
3. Maintain true occlusion/stacking behavior: a pet may be behind the topmost
   eligible window and must not be lifted above it merely because a lower
   window is also eligible.
4. Ensure child scenes and display resources are torn down safely on quit,
   replacement, and headless/Xvfb shutdown.
5. Add deterministic regression tests for stale-window handling, boundary
   geometry, occlusion ordering, and repeated startup/shutdown. Keep existing
   behavior tests intact.

## CHECK

- `make test-behavior`
- `make test-x11-refresh`
- `make test-desktop`
- `make test-gui`
- `make`

Create exactly one commit:
`Harden X11 lifecycle and monitor occlusion behavior`.
