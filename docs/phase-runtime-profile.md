# RUNTIME-1: Live pet profile switching

PHASE: RUNTIME-1 — load installed catalog packages and switch live profiles
MODE: GREEN
START_HEAD: 6c81ebe
OWN: `src/main.c`, `src/pet_package.c`, `src/pet_package.h`, runtime/profile
tests and focused runtime documentation.
DENY: `assets/`, `manifest.json`, provenance files, animation tables, audio
backend/cache implementation, GTK settings layout, renderer geometry,
window/monitor behavior, and unrelated cleanup.

## DO

1. Resolve catalog package paths against the development asset root and the
   installed `ESHEEP_DATADIR` root; never rely on the process working directory.
2. Make a selected catalog package provide its authored animation graph and
   embedded image to the active pet group, with safe fallback/error handling.
3. Preserve position, count, pause/hidden state, and current application
   lifetime when replacing a live profile. A failed replacement must leave the
   existing profile untouched.
4. Keep explicit `--package`, `--sprite`, and built-in sheep/penguin precedence
   deterministic and backward compatible.
5. Add focused tests for installed-path resolution, catalog package loading,
   successful replacement, and failed replacement rollback. Tests must prove
   the real package/image path is used rather than a metadata-only fallback.

## CHECK

- `make test-pet-package`
- `make test-pet-catalog`
- `make test-behavior`
- `make test-gui`
- `make`

No dependency installation, no destructive Git commands, and no edits outside
OWN. Create exactly one commit:
`Load catalog profiles from installed assets`.

## HANDOFF

The supervisor runs the deterministic acceptance gate, checks the exact path
allowlist, and integrates only an accepted commit. If a check fails, create a
narrow repair phase from the observed invariant rather than broadening this
contract.
