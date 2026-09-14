# UI-1: Complete settings and spritesheet selection

PHASE: UI-1 — make the full verified catalog selectable live
MODE: GREEN
START_HEAD: eee93ee
OWN: `src/main.c`, GTK/settings tests, CLI/settings documentation, and focused
UI test fixtures.
DENY: `assets/`, manifest/provenance files, package parser, audio backend/cache,
animation tables, renderer geometry, window/monitor behavior, and unrelated
cleanup.

## DO

1. Populate character and spritesheet controls from the verified catalog and
   bundled built-ins with stable IDs and useful display names.
2. Make character, package, spritesheet, and pet-count changes take effect in
   the running session without restart, preserving safe state and rolling back
   invalid selections.
3. Persist the selected profile and pet count, reload them on startup, and keep
   explicit CLI/environment/config precedence deterministic.
4. Ensure every selectable catalog option reports or handles unavailable assets
   cleanly rather than silently retaining a mismatched character.
5. Add GTK-level or deterministic integration tests for live selection,
   persistence/reload, count resizing, and failed-selection rollback.

## CHECK

- `make test-behavior`
- `make test-gui`
- `make test-cli`
- `make`

Create exactly one commit:
`Complete live character and spritesheet settings`.
