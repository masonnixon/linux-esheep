#include "pet_catalog.h"
#include <glib.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

/* Include the generated catalog data */
#include "pet_catalog_data.h"

/* Normalize a string for comparison: lowercase, replace underscores/hyphens with spaces */
static char *normalize_name(const char *name) {
    if (!name) return NULL;
    GString *norm = g_string_new(NULL);
    for (const char *p = name; *p; p++) {
        char c = g_ascii_tolower(*p);
        if (c == '_' || c == '-') c = ' ';
        g_string_append_c(norm, c);
    }
    return g_string_free(norm, FALSE);
}

EsheepPetCatalog *esheep_pet_catalog_load(const char *manifest_path, GError **error) {
    (void)manifest_path;
    (void)error;

    EsheepPetCatalog *catalog = g_new0(EsheepPetCatalog, 1);
    catalog->entries = entries;
    catalog->count = entry_count;
    catalog->upstream_revision = g_strdup("48ee8022c6b0363c79213e06e4bae608a3bdc332");
    return catalog;
}

const EsheepPetCatalogEntry *esheep_pet_catalog_lookup(const EsheepPetCatalog *catalog, const char *name) {
    if (!catalog || !name) return NULL;

    char *norm_name = normalize_name(name);

    for (int i = 0; i < catalog->count; i++) {
        const EsheepPetCatalogEntry *entry = &catalog->entries[i];

        /* Check stable name (folder) */
        char *norm_folder = normalize_name(entry->folder);
        if (strcmp(norm_folder, norm_name) == 0) {
            g_free(norm_folder);
            g_free(norm_name);
            return entry;
        }
        g_free(norm_folder);

        /* Check petname */
        char *norm_petname = normalize_name(entry->petname);
        if (strcmp(norm_petname, norm_name) == 0) {
            g_free(norm_petname);
            g_free(norm_name);
            return entry;
        }
        g_free(norm_petname);

        /* Check title */
        char *norm_title = normalize_name(entry->title);
        if (strcmp(norm_title, norm_name) == 0) {
            g_free(norm_title);
            g_free(norm_name);
            return entry;
        }
        g_free(norm_title);

        /* Check aliases */
        for (int j = 0; j < entry->alias_count; j++) {
            char *norm_alias = normalize_name(entry->aliases[j]);
            if (strcmp(norm_alias, norm_name) == 0) {
                g_free(norm_alias);
                g_free(norm_name);
                return entry;
            }
            g_free(norm_alias);
        }
    }

    g_free(norm_name);
    return NULL;
}

const EsheepPetCatalogEntry *esheep_pet_catalog_lookup_by_folder(const EsheepPetCatalog *catalog, const char *folder) {
    if (!catalog || !folder) return NULL;

    for (int i = 0; i < catalog->count; i++) {
        const EsheepPetCatalogEntry *entry = &catalog->entries[i];
        if (strcmp(entry->folder, folder) == 0) {
            return entry;
        }
    }
    return NULL;
}

int esheep_pet_catalog_count(const EsheepPetCatalog *catalog) {
    return catalog ? catalog->count : 0;
}

const EsheepPetCatalogEntry *esheep_pet_catalog_get_by_index(const EsheepPetCatalog *catalog, int index) {
    if (!catalog || index < 0 || index >= catalog->count) return NULL;
    return &catalog->entries[index];
}

void esheep_pet_catalog_free(EsheepPetCatalog *catalog) {
    if (!catalog) return;
    g_free(catalog->upstream_revision);
    g_free(catalog);
}

/* Accessor implementations */
const char *esheep_pet_catalog_entry_stable_name(const EsheepPetCatalogEntry *entry) {
    return entry ? entry->stable_name : NULL;
}

const char *esheep_pet_catalog_entry_title(const EsheepPetCatalogEntry *entry) {
    return entry ? entry->title : NULL;
}

const char *esheep_pet_catalog_entry_folder(const EsheepPetCatalogEntry *entry) {
    return entry ? entry->folder : NULL;
}

const char *esheep_pet_catalog_entry_petname(const EsheepPetCatalogEntry *entry) {
    return entry ? entry->petname : NULL;
}

const char *esheep_pet_catalog_entry_author(const EsheepPetCatalogEntry *entry) {
    return entry ? entry->author : NULL;
}

const char *esheep_pet_catalog_entry_version(const EsheepPetCatalogEntry *entry) {
    return entry ? entry->version : NULL;
}

int esheep_pet_catalog_entry_tiles_x(const EsheepPetCatalogEntry *entry) {
    return entry ? entry->tiles_x : 0;
}

int esheep_pet_catalog_entry_tiles_y(const EsheepPetCatalogEntry *entry) {
    return entry ? entry->tiles_y : 0;
}

int esheep_pet_catalog_entry_animations_count(const EsheepPetCatalogEntry *entry) {
    return entry ? entry->animations_count : 0;
}

int esheep_pet_catalog_entry_transitions_count(const EsheepPetCatalogEntry *entry) {
    return entry ? entry->transitions_count : 0;
}

int esheep_pet_catalog_entry_child_count(const EsheepPetCatalogEntry *entry) {
    return entry ? entry->child_count : 0;
}

int esheep_pet_catalog_entry_spawn_count(const EsheepPetCatalogEntry *entry) {
    return entry ? entry->spawn_count : 0;
}

int esheep_pet_catalog_entry_sound_count(const EsheepPetCatalogEntry *entry) {
    return entry ? entry->sound_count : 0;
}

gboolean esheep_pet_catalog_entry_has_sounds(const EsheepPetCatalogEntry *entry) {
    return entry ? entry->has_sounds : FALSE;
}

const char *esheep_pet_catalog_entry_transparency(const EsheepPetCatalogEntry *entry) {
    return entry ? entry->transparency : NULL;
}

const char *esheep_pet_catalog_entry_license_status(const EsheepPetCatalogEntry *entry) {
    return entry ? entry->license_status : NULL;
}

const char *esheep_pet_catalog_entry_attribution(const EsheepPetCatalogEntry *entry) {
    return entry ? entry->attribution : NULL;
}

const char *esheep_pet_catalog_entry_upstream_revision(const EsheepPetCatalogEntry *entry) {
    return entry ? entry->upstream_revision : NULL;
}

const char *esheep_pet_catalog_entry_xml_sha256(const EsheepPetCatalogEntry *entry) {
    return entry ? entry->xml_sha256 : NULL;
}

const char *esheep_pet_catalog_entry_icon_sha256(const EsheepPetCatalogEntry *entry) {
    return entry ? entry->icon_sha256 : NULL;
}

const char *esheep_pet_catalog_entry_readme_sha256(const EsheepPetCatalogEntry *entry) {
    return entry ? entry->readme_sha256 : NULL;
}

gboolean esheep_pet_catalog_entry_has_embedded_image(const EsheepPetCatalogEntry *entry) {
    return entry ? entry->has_embedded_image : FALSE;
}

gboolean esheep_pet_catalog_entry_has_file_ref(const EsheepPetCatalogEntry *entry) {
    return entry ? entry->has_file_ref : FALSE;
}

gboolean esheep_pet_catalog_entry_has_spritesheet_ref(const EsheepPetCatalogEntry *entry) {
    return entry ? entry->has_spritesheet_ref : FALSE;
}

const char **esheep_pet_catalog_entry_aliases(const EsheepPetCatalogEntry *entry) {
    return entry ? (const char **)entry->aliases : NULL;
}

int esheep_pet_catalog_entry_alias_count(const EsheepPetCatalogEntry *entry) {
    return entry ? entry->alias_count : 0;
}

gboolean esheep_pet_catalog_entry_is_available(const EsheepPetCatalogEntry *entry) {
    if (!entry) return FALSE;
    return entry->has_embedded_image || entry->has_file_ref || entry->has_spritesheet_ref;
}

const char *esheep_pet_catalog_entry_package_path(const EsheepPetCatalogEntry *entry) {
    return entry ? entry->package_path : NULL;
}
