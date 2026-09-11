# Asset provenance

This file is the install-time provenance record for the files shipped by the
Linux package. `packaging/check-provenance.py` verifies that every listed file
exists and that every upstream catalog record has attribution, an upstream
revision, and an XML hash before installation proceeds.

| Tracked file | Source and rights status |
| --- | --- |
| `assets/sheep_spritesheet.png` | Extracted from upstream `eSheep64`; project: [Adrianotiger/desktopPet](https://github.com/Adrianotiger/desktopPet), sprite rip credited to LiL_Stenly. Rights are unresolved; see `NOTICE.md`. |
| `tools/esheep_animations.xml` | Local eSheep behavior package derived from the upstream eSheep64 XML at revision `48ee8022c6b0363c79213e06e4bae608a3bdc332`; upstream rights are unresolved. |
| `assets/penguin_ice_blue_spritesheet.png` | Original artwork for linux-esheep, generated from `tools/penguin_rig.py`; covered by this repository's code/artwork terms. |
| `assets/penguin_sheet_preview.png` | Preview generated from the original linux-esheep penguin artwork. |
| `assets/penguin_ice_blue_spritesheet.md` | Local documentation for the original penguin artwork and tile map. |
| `manifest.json` | Hash and attribution inventory of the 26-package upstream catalog at revision `48ee8022c6b0363c79213e06e4bae608a3bdc332`; it is metadata, not a license grant. |
| `NOTICE.md` | Human-readable notices for third-party and local artwork. |

## Catalog boundary

The upstream checkout contains 26 package directories with embedded PNGs,
icons, README files, and (for some packages) MP3 sound payloads. Those
payloads are not tracked in this repository, and the inventory marks every
package license as `unresolved`. They are therefore not copied into the
Linux package. The installed `manifest.json` records the catalog for audit
purposes only; it does not make those packages legally or technically
available. Adding one requires tracking its payloads and a matching rights
record before the provenance gate will be extended.
