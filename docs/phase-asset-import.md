# ASSET-1: Import licensed upstream catalog assets

PHASE: ASSET-1 — import licensed upstream catalog assets
MODE: GREEN
START_HEAD: 615dc79
OWN: `assets/`, `packages/` if needed for imported package payloads,
`manifest.json`, `packaging/PROVENANCE.md`, catalog/asset import tooling,
catalog/package tests, and focused documentation.
DENY: `src/main.c`, `src/actor.c`, `src/interpreter.c`, `src/renderer.c`,
`src/esheep_audio.c`, generated animation tables, settings UI, window/monitor
behavior, and unrelated cleanup.

## DO

1. Obtain the licensed upstream Desktop Pet package payloads for every entry
   in the catalog, using the upstream repository/site as the source.
2. Preserve each package’s XML, spritesheet/image payload, and authored sound
   data without silently dropping duplicate sounds or metadata.
3. Add stable local paths, source URLs, license/attribution notes, and SHA-256
   checksums for every imported artifact.
4. Update the catalog generation/inventory so availability reflects files that
   are actually shipped, while retaining unavailable inventory records only
   when they genuinely lack a licensed source payload.
5. Add deterministic inventory/provenance tests that enumerate every imported
   character, verify package/image/audio counts and checksums, and reject an
   untracked or missing required asset.
6. Keep the existing sheep/penguin assets and all current tests compatible.

## CHECK

- `make test-pet-catalog`
- `make test-pet-package`
- `make test-provenance`
- `make test-install`
- `make`
- the asset inventory reports every catalog entry and its availability

No host dependency installation. Do not modify runtime code or generated
animation tables. Make exactly one commit:
`Import licensed upstream character assets and sounds`.
