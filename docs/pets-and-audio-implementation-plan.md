# Bundled Pets and Audio Implementation Plan

## Goal

Support every pet package published in the upstream `Adrianotiger/desktopPet`
repository, including its sprites, animation graphs, child scenes, spawn
records, and authored sounds. Preserve the current sheep behavior and keep
custom package loading useful for packages that do not contain audio.

The upstream checkout currently contains 26 pet packages. Their packages range
from small 8-animation pets to 268-animation sheep variants. Some packages
embed their spritesheet and icon as base64; sound-bearing packages embed MP3
audio as base64 in `<sounds>`. This is an asset/data migration as well as a
runtime feature, not just a new character-name switch.

## Design constraints

- Use one immutable, reference-counted/owned package representation with
  explicit destruction; do not copy package fields into `App` or create
  pet-specific runtime structs.
- Keep generated built-in data separate from runtime-loaded packages. Generated
  files are produced only by the generator and checked with a drift gate.
- Preserve all authored records, including repeated sound animation IDs, in
  source order. Do not deduplicate or silently reinterpret upstream data.
- Keep decoding and playback asynchronous and bounded. Audio must never run on
  the GTK animation tick or block package loading indefinitely.
- Store provenance, upstream revision, SHA-256 hashes, license text, and
  attribution for every imported XML, image, and audio payload. A package with
  unresolved redistribution rights is inventory-only until cleared.
- Prefer extracted immutable assets plus a reproducible extraction tool over
  hand-maintained copies. Retain original XML for parity and custom-package
  compatibility.
- Make optional audio capability explicit at configure/build time and runtime;
  silent operation must remain a first-class tested mode.

## Acceptance definition

For every upstream pet package:

1. The package can be selected by stable name from the CLI/configuration.
2. Its embedded or extracted spritesheet loads with the authored tile grid.
3. Its animation, transition, child, spawn, and expression data validates and
   runs without truncation caused by current fixed-size assumptions.
4. Every authored sound record is decoded and can be scheduled for its
   animation, with probability/loop behavior preserved.
5. Missing, malformed, unsupported, or silent audio never crashes the pet;
   normal animation continues with an explicit diagnostic.
6. Installed packages and assets are included in the staged distribution with
   attribution/license records.
7. Existing sheep/penguin, multi-sheep, X11 landing, monitor, occlusion,
   transition, and child-scene tests remain green.

## Phase sequence

Current status: PET-0 accepted in `9396cb7`. PET-1B (image/sound package
model and parser) is accepted in `02df328`; it parses all 26 upstream XML
packages, including embedded images and sound-bearing packages. The remaining
PET-1 metadata/runtime work is queued. AUDIO-1 is queued and may proceed
independently after PET-0's asset/provenance boundary.

### Active handoff: PET-1C — embedded image runtime integration

MODE: GREEN

OWN: `src/main.c`, `src/pet_package.h`, `src/pet_package.c`,
`tests/test_pet_package.c`, and focused documentation only.

DENY: generated animation tables, `assets/`, catalog files, audio playback,
window/monitor behavior, and unrelated cleanup.

DO:

1. When a loaded package has embedded PNG data and no explicit `--sprite`,
   load that image directly from owned package bytes using GTK/GdkPixbuf.
2. Preserve explicit sprite precedence: `--sprite`, environment, config,
   then package embedded image, then the existing built-in default.
3. Validate the embedded image against the package tile grid and preserve the
   existing diagnostics for incompatible sheets.
4. Add focused tests proving embedded-image selection and explicit override;
   do not require a live desktop for these tests.

CHECK: `make test-pet-package`, existing parser/runtime tests, `make`, and a
focused embedded-image precedence test.

COMMIT: `Use embedded package images at runtime`

STOP: exactly one commit, no dependency installation, no reset/restore/clean,
and no changes outside OWN.

### PET-0 — freeze upstream inventory and asset policy

Record the upstream revision, all 26 package names, animation/transition/child
counts, image dimensions, sound counts, licenses/attribution, and hashes of
source XML/assets. Decide whether the repository stores extracted binary
assets, original XML with base64, or both. Prefer reproducible extraction from
checked-in source plus a manifest, and do not silently redistribute assets whose
license/attribution is unclear.

Checks: inventory script, XML parse, image/audio MIME detection, license report.

### PET-1 — generalize package data model and parser

Introduce an owned `EsheepPackage` representation with GLib containers and
single-owner cleanup boundaries. Replace sheep-specific and fixed-capacity
assumptions with validated dynamic package data sized for the largest upstream package. Add package metadata,
character name, tile dimensions, embedded icon/spritesheet fields, sounds, and
all authored animation records. Preserve generated built-in sheep data and
the current generated-data synchronization gate.

Checks: parser unit tests for every upstream package, malformed base64/XML,
large animation counts, duplicate IDs, invalid references, and memory cleanup.

### PET-2 — extract and register the complete pet catalog

Add the upstream packages to a versioned asset/catalog layout generated from a
manifest, not hand-wired conditionals. Register stable
names and aliases, including sheep color variants, bunny, ham ham, fox,
Grian, Mareep, Neko variants, Pikachu, Pingus, Pokémon/anime pets, skeleton,
zombie, and the remaining upstream packages. Add `--list-characters` and make
`--character NAME` resolve the catalog. Keep `--package PATH` for arbitrary
custom packages.

Checks: catalog enumerates every package, each name resolves, each package's
sprite dimensions match its XML, and CLI/config precedence remains intact.

### PET-3 — rendering compatibility for non-sheep packages

Verify tile sizes, transparency models, alpha bounds, flips, child scenes,
large scenes, spawn coordinates, animation IDs, and package-specific expression
forms. Extend the renderer only where the upstream package contract requires
it; avoid sheep-specific coordinate heuristics. Add per-package smoke renders
and deterministic visual snapshots for representative simple, child-scene,
large, and off-screen spawn packages.

Checks: all catalog packages launch under Xvfb, render at least one normal and
one special animation, and terminate cleanly.

### AUDIO-1 — audio backend decision and abstraction

Audit available supported libraries inside the project validation container;
never install a dependency on the host. Select the smallest maintained
asynchronous backend capable of decoding upstream MP3 data, preferably
GStreamer if the project/container already supports it. Hide it behind a
small opaque `EsheepAudio` interface with explicit init/shutdown ownership.
Keep playback off the GTK animation tick, cap concurrent voices, and provide a
build-time capability check plus a no-audio fallback with a clear diagnostic.

Checks: backend init/shutdown, decode failure, cancellation, repeated events,
multiple pets, no-display/headless mode, and no blocking on the GTK main loop.

### AUDIO-2 — parse, cache, and schedule authored sounds

Decode `<sounds>` base64 payloads into a managed cache, preserve animation ID,
probability, loop count, and source order, and trigger sounds at the same
animation lifecycle points as the upstream behavior. Give sound selection its
own deterministic RNG stream so movement sequences do not change. Preserve
duplicate animation IDs as separate records and test their authored ordering.

Checks: fixture audio, all sound-bearing packages, probability seed tests,
loop tests, cache lifetime, malformed payloads, and sound-disabled operation.

### AUDIO-3 — controls and user configuration

Add configuration, CLI, and environment controls for enabled/disabled audio,
master volume, and maximum concurrent voices with one documented precedence
order. Add the setting to the existing settings UI/tray action only through a
small adapter; audio initialization must remain optional for silent packages
and test runs.

Checks: config/env/CLI precedence, volume bounds, runtime toggle, and
multi-sheep performance at 1/5/10/32 pets.

### PET-4 — packaging, attribution, and documentation

Install the full catalog, extracted sprites, audio, manifest, and license/
attribution files. Fail packaging when a required asset has no provenance
record rather than shipping an untracked binary.
Update README/man pages with catalog names, selection examples, audio controls,
asset provenance, and known backend limitations. Remove the stale statement
that UFO/pilot tiles are unused.

Checks: staged install contains every required file, clean checkout reproduces
generated data/assets, and attribution files cover every upstream package.

### PET-5 — final parity and regression acceptance

Run package inventory/parity tests, all parser/renderer/audio tests, complete
`make test`, optimized build, staged install, catalog rendering, CLI checks,
and representative live runs for every pet family. Record any intentional
differences from upstream rather than silently presenting them as parity.

## Parallelization

After PET-0, PET-1 can proceed independently of AUDIO-1. PET-2 can proceed
alongside AUDIO-1 once the asset policy is fixed. PET-3 depends on PET-1 and
PET-2. AUDIO-2 depends on PET-1 and AUDIO-1. PET-4 waits for PET-2 and the
audio data layout. PET-5 waits for all implementation phases.

The safest first implementation wave is PET-0, then PET-1 plus AUDIO-1 in
separate worktrees, followed by PET-2 and AUDIO-2. PET-0 owns the single source
manifest and provenance records; no other phase may edit them concurrently.
Do not edit generated tables or the same package manifest concurrently without
a clear ownership boundary.

## Explicit non-goals for this plan

- Native Wayland window discovery and compositor-specific positioning.
- Rewriting the upstream animation behavior into a different semantic model.
- Installing dependencies on the host; build and validation dependencies must
  be supplied through the project/container workflow.
- Claiming exact upstream audio parity when an asset license or codec backend
  is unavailable; such cases must be reported and gated explicitly.
