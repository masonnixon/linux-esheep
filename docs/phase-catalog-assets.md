# CAT-1: Catalog asset availability

PHASE: CAT-1 — make catalog character availability truthful
MODE: PROOF
START_HEAD: ca88ab9
OWN: `src/pet_catalog.c`, `src/pet_catalog.h`, generated catalog data only if
the generator is changed, catalog tests, and focused documentation.
DENY: `src/main.c`, animation tables, audio backend, window/monitor behavior,
unproven upstream binary downloads, and unrelated cleanup.

## Goal

Ensure the character catalog exposes only characters that the current package
loader can actually instantiate, while preserving the complete upstream
inventory as metadata. A settings dropdown or `--list-characters` must not
claim a character is usable merely because an upstream inventory record says
that an asset existed.

## DO

1. Trace the current catalog availability flags through package-path and image
   loading, identifying the exact conditions that make a character runnable.
2. Correct the availability predicate or generated-data interpretation so it
   agrees with the files and embedded payloads shipped by this repository.
3. Preserve complete inventory enumeration and explicit unavailable markers.
4. Add tests proving that every advertised available character has a loadable
   package/image path, and that unavailable inventory records remain listed but
   rejected with a useful diagnostic.
5. Update focused catalog documentation with the distinction between inventory
   records and runnable packages.

## CHECK

- `make test-pet-catalog`
- `make test-pet-package`
- `make test-cli`
- `make`
- no network download or host dependency installation

COMMIT: `Make catalog availability match shipped assets`
STOP: exactly one commit; no install, reset, restore, clean, or changes outside
OWN.
