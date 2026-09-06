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

### PET-0 — freeze upstream inventory and asset policy

Record the upstream revision, all 26 package names, animation/transition/child
counts, image dimensions, sound counts, licenses/attribution, and hashes of
source XML/assets. Decide whether the repository stores extracted binary
assets, original XML with base64, or both. Prefer reproducible extraction from
checked-in source plus a manifest, and do not silently redistribute assets whose
license/attribution is unclear.

Checks: inventory script, XML parse, image/audio MIME detection, license report.

### PET-1 — generalize package data model and parser

Replace sheep-specific and fixed-capacity assumptions with validated dynamic
package data sized for the largest upstream package. Add package metadata,
character name, tile dimensions, embedded icon/spritesheet fields, sounds, and
all authored animation records. Preserve generated built-in sheep data and
the current generated-data synchronization gate.

Checks: parser unit tests for every upstream package, malformed base64/XML,
large animation counts, duplicate IDs, invalid references, and memory cleanup.

### PET-2 — extract and register the complete pet catalog

Add the upstream packages to a versioned asset/catalog layout. Register stable
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

Audit available supported libraries in the build/container environment. Use an
idiomatic asynchronous backend capable of decoding the upstream embedded MP3
data, preferably GStreamer when available, behind a small `EsheepAudio`
interface. Keep audio playback off the GTK animation tick and cap/consolidate
concurrent voices for multiple pets. Provide a build-time capability check and
a no-audio fallback with a clear warning.

Checks: backend init/shutdown, decode failure, cancellation, repeated events,
multiple pets, no-display/headless mode, and no blocking on the GTK main loop.

### AUDIO-2 — parse, cache, and schedule authored sounds

Decode `<sounds>` base64 payloads into memory or a managed cache, preserve
animation ID, probability, and loop count, and trigger sounds at the same
animation lifecycle points as the upstream behavior. Define deterministic RNG
semantics for sound probability without perturbing movement RNG. Ensure each
package can have duplicate upstream sound IDs only when the source semantics
define how they combine; otherwise report and choose a documented policy.

Checks: fixture audio, all sound-bearing packages, probability seed tests,
loop tests, cache lifetime, malformed payloads, and sound-disabled operation.

### AUDIO-3 — controls and user configuration

Add configuration and CLI/environment controls for enabled/disabled audio,
master volume, and maximum concurrent voices. Add the setting to the existing
settings UI/tray action if practical, without making audio initialization
mandatory for silent packages or test runs.

Checks: config/env/CLI precedence, volume bounds, runtime toggle, and
multi-sheep performance at 1/5/10/32 pets.

### PET-4 — packaging, attribution, and documentation

Install the full catalog, extracted sprites, and required audio/license files.
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

The safest first implementation wave is PET-1 plus AUDIO-1 in separate
worktrees, followed by PET-2 and AUDIO-2. Do not edit generated tables or the
same package manifest concurrently without a clear ownership boundary.

## Explicit non-goals for this plan

- Native Wayland window discovery and compositor-specific positioning.
- Rewriting the upstream animation behavior into a different semantic model.
- Installing dependencies on the host; build and validation dependencies must
  be supplied through the project/container workflow.
- Claiming exact upstream audio parity when an asset license or codec backend
  is unavailable; such cases must be reported and gated explicitly.
