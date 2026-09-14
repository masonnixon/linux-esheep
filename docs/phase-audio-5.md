# AUDIO-5: Connect imported sounds to live profiles

PHASE: AUDIO-5 — validate and play authored package sounds
MODE: GREEN
START_HEAD: 6d769bc
OWN: `src/esheep_audio.c`, `src/esheep_audio.h`, `src/esheep_sound_cache.c`,
`src/esheep_sound_cache.h`, audio tests, and focused audio documentation.
DENY: runtime profile/GTK code, asset payloads, manifest/provenance files,
animation tables, renderer/window/monitor behavior, and unrelated cleanup.

## DO

1. Make embedded sound records from imported packages usable by the existing
   asynchronous audio backend, preserving authored ordering, probability, and
   loop metadata.
2. Keep audio optional: silent packages, disabled audio, missing players, and
   malformed sound records must not affect movement or startup.
3. Add deterministic tests with a fake player/cache for payload decoding,
   selection probability, loop handling, and graceful unavailable audio.
4. Document one real-player smoke command without making it a test prerequisite.

## CHECK

- `make test-esheep-audio`
- `make test-audio-controls`
- `make test-sound-cache`
- `make test-pet-package`
- `make`

Create exactly one commit:
`Connect imported sounds to live profiles`.
