# ASSET-1R: Repair and accept imported catalog assets

PHASE: ASSET-1R — repair ASSET-1 packaging and provenance
MODE: ACCEPT
START_HEAD: d4ab119
WORKTREE: `/tmp/for-agents/esheep-asset1`
OWN: Existing ASSET-1 asset payloads and metadata in that worktree, generated
manifest/provenance/inventory files, asset-import tooling, catalog/package
tests, and focused documentation.
DENY: runtime C/GTK code, animation tables, audio backend implementation,
window/monitor behavior, and unrelated cleanup.

## Required repair

1. Inspect the existing untracked ASSET-1 output. Preserve valid upstream XML,
   image, icon, and sound payloads; do not re-download or discard assets merely
   to simplify the tree.
2. Select one canonical shipped asset layout. Remove only the exact duplicate
   copy created by ASSET-1; do not use broad clean/reset/restore commands.
3. Make the manifest and provenance records internally consistent with the
   user-confirmed licensing permission. Record source URLs, attribution,
   license status, and SHA-256 checksums for every shipped artifact.
4. Ensure the catalog inventory reports the actual package/image/sound counts
   and distinguishes imported assets from metadata-only upstream records.
5. Add or repair deterministic inventory/provenance tests for the complete
   imported set.
6. Stage all intended files and create exactly one real Git commit in this
   worktree: `Import licensed upstream character assets and sounds`.

## Acceptance checks

- `make test-pet-catalog`
- `make test-pet-package`
- `make test-provenance`
- `make test-install`
- `make`
- worktree clean after commit
- committed paths remain within OWN

No host dependency installation, no destructive Git commands, and no runtime
code changes. Report only the compact handoff result format.
