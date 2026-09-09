#ifndef ESHEEP_PET_CATALOG_H
#define ESHEEP_PET_CATALOG_H

#include <glib.h>

/* Catalog entry representing a single pet package from the upstream inventory. */
typedef struct EsheepPetCatalogEntry {
    const char *stable_name;
    const char *title;
    const char *folder;
    const char *petname;
    const char *author;
    const char *version;
    int tiles_x;
    int tiles_y;
    int animations_count;
    int transitions_count;
    int child_count;
    int spawn_count;
    int sound_count;
    gboolean has_sounds;
    const char *transparency;
    const char *license_status;
    const char *attribution;
    const char *upstream_revision;
    const char *xml_sha256;
    const char *icon_sha256;
    const char *readme_sha256;
    gboolean has_embedded_image;
    gboolean has_file_ref;
    gboolean has_spritesheet_ref;
    char **aliases;
    int alias_count;
    const char *package_path;
} EsheepPetCatalogEntry;

/* Catalog type. */
typedef struct EsheepPetCatalog {
    const EsheepPetCatalogEntry *entries;
    int count;
    char *upstream_revision;
} EsheepPetCatalog;

/* Load the catalog from the manifest.json file.
 * Returns a new catalog that must be freed with esheep_pet_catalog_free().
 * On error, returns NULL and sets error. */
EsheepPetCatalog *esheep_pet_catalog_load(const char *manifest_path, GError **error);

/* Look up a catalog entry by stable name or alias (case-insensitive).
 * Returns the entry, or NULL if not found. */
const EsheepPetCatalogEntry *esheep_pet_catalog_lookup(const EsheepPetCatalog *catalog, const char *name);

/* Look up a catalog entry by folder name (e.g., "blue_sheep").
 * Returns the entry, or NULL if not found. */
const EsheepPetCatalogEntry *esheep_pet_catalog_lookup_by_folder(const EsheepPetCatalog *catalog, const char *folder);

/* Get the number of entries in the catalog. */
int esheep_pet_catalog_count(const EsheepPetCatalog *catalog);

/* Get an entry by index (0-based). */
const EsheepPetCatalogEntry *esheep_pet_catalog_get_by_index(const EsheepPetCatalog *catalog, int index);

/* Free a catalog. */
void esheep_pet_catalog_free(EsheepPetCatalog *catalog);

/* Entry accessors - the catalog owns all returned strings. */
const char *esheep_pet_catalog_entry_stable_name(const EsheepPetCatalogEntry *entry);
const char *esheep_pet_catalog_entry_title(const EsheepPetCatalogEntry *entry);
const char *esheep_pet_catalog_entry_folder(const EsheepPetCatalogEntry *entry);
const char *esheep_pet_catalog_entry_petname(const EsheepPetCatalogEntry *entry);
const char *esheep_pet_catalog_entry_author(const EsheepPetCatalogEntry *entry);
const char *esheep_pet_catalog_entry_version(const EsheepPetCatalogEntry *entry);
int esheep_pet_catalog_entry_tiles_x(const EsheepPetCatalogEntry *entry);
int esheep_pet_catalog_entry_tiles_y(const EsheepPetCatalogEntry *entry);
int esheep_pet_catalog_entry_animations_count(const EsheepPetCatalogEntry *entry);
int esheep_pet_catalog_entry_transitions_count(const EsheepPetCatalogEntry *entry);
int esheep_pet_catalog_entry_child_count(const EsheepPetCatalogEntry *entry);
int esheep_pet_catalog_entry_spawn_count(const EsheepPetCatalogEntry *entry);
int esheep_pet_catalog_entry_sound_count(const EsheepPetCatalogEntry *entry);
gboolean esheep_pet_catalog_entry_has_sounds(const EsheepPetCatalogEntry *entry);
const char *esheep_pet_catalog_entry_transparency(const EsheepPetCatalogEntry *entry);
const char *esheep_pet_catalog_entry_license_status(const EsheepPetCatalogEntry *entry);
const char *esheep_pet_catalog_entry_attribution(const EsheepPetCatalogEntry *entry);
const char *esheep_pet_catalog_entry_upstream_revision(const EsheepPetCatalogEntry *entry);
const char *esheep_pet_catalog_entry_xml_sha256(const EsheepPetCatalogEntry *entry);
const char *esheep_pet_catalog_entry_icon_sha256(const EsheepPetCatalogEntry *entry);
const char *esheep_pet_catalog_entry_readme_sha256(const EsheepPetCatalogEntry *entry);
gboolean esheep_pet_catalog_entry_has_embedded_image(const EsheepPetCatalogEntry *entry);
gboolean esheep_pet_catalog_entry_has_file_ref(const EsheepPetCatalogEntry *entry);
gboolean esheep_pet_catalog_entry_has_spritesheet_ref(const EsheepPetCatalogEntry *entry);
const char **esheep_pet_catalog_entry_aliases(const EsheepPetCatalogEntry *entry);
int esheep_pet_catalog_entry_alias_count(const EsheepPetCatalogEntry *entry);

/* Check if a package is available (has embedded image or file ref).
 * Packages without available assets should still be enumerated but marked
 * as unavailable for actual use. */
gboolean esheep_pet_catalog_entry_is_available(const EsheepPetCatalogEntry *entry);

/* Get the package path for a catalog entry (relative to upstream Pets dir).
 * The returned string is owned by the catalog. */
const char *esheep_pet_catalog_entry_package_path(const EsheepPetCatalogEntry *entry);

#endif /* ESHEEP_PET_CATALOG_H */
