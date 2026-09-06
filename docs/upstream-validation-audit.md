# Upstream Validation Audit

Reference: `Adrianotiger/desktopPet`, source revision `48ee802` (2026-09-05).
The DeepWiki page was unavailable to the repository-review tool, so the public
checkout was used as the fallback reference.

## Animation and transition comparison

The upstream package is `src/Resources/animations.xml` and contains 54
animation records. The local package contains those same 54 records plus 13
local extension records, for 67 total. All 54 common animation records match
after ignoring XML formatting whitespace, including frame sequences, timing,
motion values, and authored transitions.

The upstream package contains 93 `<next>` records. The local package contains
106 animation-level records plus four child records, exported as 110 runtime
transition rows. The first 93 animation-level records are the upstream graph;
the additional records are the local extension graph.

The local package has the same four spawn records and probabilities as
upstream:

| Spawn | Target | Probability |
|---:|---:|---:|
| 1 | 1 | 20% |
| 2 | 1 | 80% |
| 3 | 21 | 3% |
| 4 | 28 | 3% |

## Confirmed divergences

1. The upstream package has three child records: bath, flower, and black
   sheep/UFO. The local package adds a fourth spacecraft/pilot child record.
2. The upstream black-sheep child placement is `x=-imageW`, `y=imageY`.
   The local package uses `x=-imageW-8`, `y=imageY`, which is a deliberate
   local visual-spacing change and needs a visual decision against upstream.
3. Local animations 55-67 are art-completeness extensions. They make filled
   spritesheet tiles reviewable, but they are not present in the upstream
   behavior graph and most have no normal inbound transition.
4. The local README still contains an obsolete statement that UFO and pilot
   tiles are unused. That documentation must be corrected or removed.

## Feature-scope differences requiring follow-up

The upstream project advertises XML-configurable pets and includes multiple
pet packages. The local runtime currently supports the bundled sheep and
ice-blue penguin plus custom package/spritesheet loading. It does not yet
provide the upstream pet catalog as bundled selectable characters.

The upstream project includes sound support; the local project explicitly
documents sound as unimplemented because the recovered package has no local
audio assets or sound metadata.

Native Wayland window discovery/positioning remains unsupported locally;
XWayland fallback is supported. Native compositor-specific behavior therefore
remains outside this validation branch unless separately scoped.

## Required next decisions

- Decide whether “upstream parity” means preserving only the upstream 54-record
  behavior graph or retaining the local 13-record art-completeness extensions.
- Decide whether to restore black-sheep placement to the upstream `-imageW`
  value or retain the local eight-pixel spacing as an intentional visual fix.
- Correct the stale README asset statement.
- Add or explicitly defer the upstream pet catalog, sound, and richer package
  grammar; these are not animation-transition mismatches.

## Evidence

- `python3 /tmp/for-agents/audit_upstream.py`: 54 common animation records,
  zero common animation-definition differences, three upstream child records,
  four local child records.
- `make test-transition-parity`: all 110 local transition rows match the
  authored local package and local golden expectations.
- `make test-assets`: all referenced local sprite tiles validate.
