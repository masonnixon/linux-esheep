# Asset provenance

This file is the install-time provenance record for the files shipped by the
Linux package. `packaging/check-provenance.py` verifies that every listed file
exists and that every upstream catalog record has attribution, an upstream
revision, matching SHA-256 hashes, and the user-confirmed rights status before
installation proceeds.

| Tracked file | Source and rights status |
| --- | --- |
| `assets/sheep_spritesheet.png` | Extracted from upstream `eSheep64`; project: [Adrianotiger/desktopPet](https://github.com/Adrianotiger/desktopPet), sprite rip credited to LiL_Stenly. Rights are unresolved; see `NOTICE.md`. |
| `tools/esheep_animations.xml` | Local eSheep behavior package derived from the upstream eSheep64 XML at revision `48ee8022c6b0363c79213e06e4bae608a3bdc332`; upstream rights are unresolved. |
| `assets/penguin_ice_blue_spritesheet.png` | Original artwork for linux-esheep, generated from `tools/penguin_rig.py`; covered by this repository's code/artwork terms. |
| `assets/penguin_sheet_preview.png` | Preview generated from the original linux-esheep penguin artwork. |
| `assets/penguin_ice_blue_spritesheet.md` | Local documentation for the original penguin artwork and tile map. |
| `assets/Pets/<folder>/animations.xml`, `icon.png`, `README.md` | The 26 imported upstream character packages, copied without altering authored XML, embedded sprite, or embedded sound payloads. Each file is checked against `manifest.json`. Source: [Adrianotiger/desktopPet](https://github.com/Adrianotiger/desktopPet), revision `48ee8022c6b0363c79213e06e4bae608a3bdc332`. Distribution is authorized by the user's confirmed licensing permission; attribution remains with each package's upstream author. |
| `manifest.json` | Hash and attribution inventory of the 26-package upstream catalog at revision `48ee8022c6b0363c79213e06e4bae608a3bdc332`; each package is marked `user-confirmed`. It is an audit record, not a replacement for the upstream authors' terms. |
| `NOTICE.md` | Human-readable notices for third-party and local artwork. |

## Catalog boundary

The upstream checkout contains 26 package directories with embedded PNGs,
icons, README files, and (for some packages) MP3 sound payloads. The packages
are now tracked under `assets/Pets`; sound bytes remain embedded in their
authored XML and are not silently extracted or altered. `user-confirmed` is a
project record of the licensing permission confirmed for this import, while
the upstream package terms and author attributions remain authoritative.
