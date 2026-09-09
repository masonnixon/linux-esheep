#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/pet_catalog.h"

static void test_catalog_load(void) {
    printf("test: catalog loads correctly\n");
    GError *error = NULL;
    EsheepPetCatalog *catalog = esheep_pet_catalog_load("manifest.json", &error);
    assert(catalog != NULL);
    assert(error == NULL);
    assert(esheep_pet_catalog_count(catalog) == 26);
    esheep_pet_catalog_free(catalog);
    printf("OK\n");
}

static void test_catalog_lookup_by_stable_name(void) {
    printf("test: lookup by stable name (folder)\n");
    GError *error = NULL;
    EsheepPetCatalog *catalog = esheep_pet_catalog_load("manifest.json", &error);
    
    const EsheepPetCatalogEntry *entry = esheep_pet_catalog_lookup(catalog, "bbunny");
    assert(entry != NULL);
    assert(strcmp(esheep_pet_catalog_entry_stable_name(entry), "bbunny") == 0);
    assert(strcmp(esheep_pet_catalog_entry_title(entry), "Buster Bunny") == 0);
    
    entry = esheep_pet_catalog_lookup(catalog, "blue_sheep");
    assert(entry != NULL);
    assert(strcmp(esheep_pet_catalog_entry_stable_name(entry), "blue_sheep") == 0);
    assert(strcmp(esheep_pet_catalog_entry_petname(entry), "Ben") == 0);
    
    esheep_pet_catalog_free(catalog);
    printf("OK\n");
}

static void test_catalog_lookup_by_petname(void) {
    printf("test: lookup by petname\n");
    GError *error = NULL;
    EsheepPetCatalog *catalog = esheep_pet_catalog_load("manifest.json", &error);
    
    const EsheepPetCatalogEntry *entry = esheep_pet_catalog_lookup(catalog, "Bunny");
    assert(entry != NULL);
    assert(strcmp(esheep_pet_catalog_entry_folder(entry), "bbunny") == 0);
    
    entry = esheep_pet_catalog_lookup(catalog, "Ben");
    assert(entry != NULL);
    assert(strcmp(esheep_pet_catalog_entry_folder(entry), "blue_sheep") == 0);
    
    entry = esheep_pet_catalog_lookup(catalog, "Gus");
    assert(entry != NULL);
    assert(strcmp(esheep_pet_catalog_entry_folder(entry), "green_sheep") == 0);
    
    esheep_pet_catalog_free(catalog);
    printf("OK\n");
}

static void test_catalog_lookup_by_title(void) {
    printf("test: lookup by title\n");
    GError *error = NULL;
    EsheepPetCatalog *catalog = esheep_pet_catalog_load("manifest.json", &error);
    
    const EsheepPetCatalogEntry *entry = esheep_pet_catalog_lookup(catalog, "Buster Bunny");
    assert(entry != NULL);
    assert(strcmp(esheep_pet_catalog_entry_folder(entry), "bbunny") == 0);
    
    entry = esheep_pet_catalog_lookup(catalog, "gSheep Blue");
    assert(entry != NULL);
    assert(strcmp(esheep_pet_catalog_entry_folder(entry), "blue_sheep") == 0);
    
    esheep_pet_catalog_free(catalog);
    printf("OK\n");
}

static void test_catalog_lookup_by_aliases(void) {
    printf("test: lookup by aliases\n");
    GError *error = NULL;
    EsheepPetCatalog *catalog = esheep_pet_catalog_load("manifest.json", &error);
    
    const EsheepPetCatalogEntry *entry = esheep_pet_catalog_lookup(catalog, "bunny");
    assert(entry != NULL);
    assert(strcmp(esheep_pet_catalog_entry_folder(entry), "bbunny") == 0);
    
    entry = esheep_pet_catalog_lookup(catalog, "buster bunny");
    assert(entry != NULL);
    assert(strcmp(esheep_pet_catalog_entry_folder(entry), "bbunny") == 0);
    
    entry = esheep_pet_catalog_lookup(catalog, "blue sheep");
    assert(entry != NULL);
    assert(strcmp(esheep_pet_catalog_entry_folder(entry), "blue_sheep") == 0);
    
    entry = esheep_pet_catalog_lookup(catalog, "ben");
    assert(entry != NULL);
    assert(strcmp(esheep_pet_catalog_entry_folder(entry), "blue_sheep") == 0);
    
    entry = esheep_pet_catalog_lookup(catalog, "gsheep blue");
    assert(entry != NULL);
    assert(strcmp(esheep_pet_catalog_entry_folder(entry), "blue_sheep") == 0);
    
    entry = esheep_pet_catalog_lookup(catalog, "esheep");
    assert(entry != NULL);
    assert(strcmp(esheep_pet_catalog_entry_folder(entry), "esheep64") == 0);
    
    entry = esheep_pet_catalog_lookup(catalog, "fox mate");
    assert(entry != NULL);
    assert(strcmp(esheep_pet_catalog_entry_folder(entry), "fox") == 0);
    
    entry = esheep_pet_catalog_lookup(catalog, "gus");
    assert(entry != NULL);
    assert(strcmp(esheep_pet_catalog_entry_folder(entry), "green_sheep") == 0);
    
    entry = esheep_pet_catalog_lookup(catalog, "neko mate");
    assert(entry != NULL);
    assert(strcmp(esheep_pet_catalog_entry_folder(entry), "neko") == 0);
    
    entry = esheep_pet_catalog_lookup(catalog, "pikachu");
    assert(entry != NULL);
    assert(strcmp(esheep_pet_catalog_entry_folder(entry), "pikachu") == 0);
    
    entry = esheep_pet_catalog_lookup(catalog, "pingus");
    assert(entry != NULL);
    assert(strcmp(esheep_pet_catalog_entry_folder(entry), "pingus") == 0);
    
    entry = esheep_pet_catalog_lookup(catalog, "pink fox");
    assert(entry != NULL);
    assert(strcmp(esheep_pet_catalog_entry_folder(entry), "pink_fox") == 0);
    
    entry = esheep_pet_catalog_lookup(catalog, "pearl");
    assert(entry != NULL);
    assert(strcmp(esheep_pet_catalog_entry_folder(entry), "pink_sheep") == 0);
    
    entry = esheep_pet_catalog_lookup(catalog, "patsu");
    assert(entry != NULL);
    assert(strcmp(esheep_pet_catalog_entry_folder(entry), "purple_sheep") == 0);
    
    entry = esheep_pet_catalog_lookup(catalog, "rick");
    assert(entry != NULL);
    assert(strcmp(esheep_pet_catalog_entry_folder(entry), "red_sheep") == 0);
    
    entry = esheep_pet_catalog_lookup(catalog, "goku");
    assert(entry != NULL);
    assert(strcmp(esheep_pet_catalog_entry_folder(entry), "ssj-goku") == 0);
    
    entry = esheep_pet_catalog_lookup(catalog, "yogurt");
    assert(entry != NULL);
    assert(strcmp(esheep_pet_catalog_entry_folder(entry), "yellow_sheep") == 0);
    
    entry = esheep_pet_catalog_lookup(catalog, "zombie");
    assert(entry != NULL);
    assert(strcmp(esheep_pet_catalog_entry_folder(entry), "zombie") == 0);
    
    esheep_pet_catalog_free(catalog);
    printf("OK\n");
}

static void test_catalog_case_insensitive(void) {
    printf("test: case insensitive lookup\n");
    GError *error = NULL;
    EsheepPetCatalog *catalog = esheep_pet_catalog_load("manifest.json", &error);
    
    const EsheepPetCatalogEntry *entry = esheep_pet_catalog_lookup(catalog, "BUNNY");
    assert(entry != NULL);
    assert(strcmp(esheep_pet_catalog_entry_folder(entry), "bbunny") == 0);
    
    entry = esheep_pet_catalog_lookup(catalog, "Blue Sheep");
    assert(entry != NULL);
    assert(strcmp(esheep_pet_catalog_entry_folder(entry), "blue_sheep") == 0);
    
    entry = esheep_pet_catalog_lookup(catalog, "BEN");
    assert(entry != NULL);
    assert(strcmp(esheep_pet_catalog_entry_folder(entry), "blue_sheep") == 0);
    
    entry = esheep_pet_catalog_lookup(catalog, "Buster Bunny");
    assert(entry != NULL);
    
    esheep_pet_catalog_free(catalog);
    printf("OK\n");
}

static void test_catalog_invalid_name(void) {
    printf("test: invalid name returns NULL\n");
    GError *error = NULL;
    EsheepPetCatalog *catalog = esheep_pet_catalog_load("manifest.json", &error);
    
    const EsheepPetCatalogEntry *entry = esheep_pet_catalog_lookup(catalog, "nonexistent");
    assert(entry == NULL);
    
    entry = esheep_pet_catalog_lookup(catalog, "dragon");
    assert(entry == NULL);
    
    entry = esheep_pet_catalog_lookup(catalog, "");
    assert(entry == NULL);
    
    entry = esheep_pet_catalog_lookup(catalog, NULL);
    assert(entry == NULL);
    
    esheep_pet_catalog_free(catalog);
    printf("OK\n");
}

static void test_catalog_lookup_by_folder(void) {
    printf("test: lookup by folder\n");
    GError *error = NULL;
    EsheepPetCatalog *catalog = esheep_pet_catalog_load("manifest.json", &error);
    
    const EsheepPetCatalogEntry *entry = esheep_pet_catalog_lookup_by_folder(catalog, "bbunny");
    assert(entry != NULL);
    assert(strcmp(esheep_pet_catalog_entry_folder(entry), "bbunny") == 0);
    
    entry = esheep_pet_catalog_lookup_by_folder(catalog, "blue_sheep");
    assert(entry != NULL);
    assert(strcmp(esheep_pet_catalog_entry_folder(entry), "blue_sheep") == 0);
    
    entry = esheep_pet_catalog_lookup_by_folder(catalog, "nonexistent");
    assert(entry == NULL);
    
    esheep_pet_catalog_free(catalog);
    printf("OK\n");
}

static void test_catalog_enumeration(void) {
    printf("test: catalog enumeration\n");
    GError *error = NULL;
    EsheepPetCatalog *catalog = esheep_pet_catalog_load("manifest.json", &error);
    
    int count = esheep_pet_catalog_count(catalog);
    assert(count == 26);
    
    for (int i = 0; i < count; i++) {
        const EsheepPetCatalogEntry *entry = esheep_pet_catalog_get_by_index(catalog, i);
        assert(entry != NULL);
        assert(entry->stable_name != NULL);
        assert(entry->title != NULL);
        assert(entry->folder != NULL);
        assert(entry->petname != NULL);
        assert(entry->tiles_x > 0);
        assert(entry->tiles_y > 0);
        assert(entry->animations_count > 0);
        assert(entry->transitions_count >= 0);
    }
    
    const EsheepPetCatalogEntry *entry = esheep_pet_catalog_get_by_index(catalog, count);
    assert(entry == NULL);
    
    entry = esheep_pet_catalog_get_by_index(catalog, -1);
    assert(entry == NULL);
    
    esheep_pet_catalog_free(catalog);
    printf("OK\n");
}

static void test_catalog_availability(void) {
    printf("test: availability check\n");
    GError *error = NULL;
    EsheepPetCatalog *catalog = esheep_pet_catalog_load("manifest.json", &error);
    
    for (int i = 0; i < esheep_pet_catalog_count(catalog); i++) {
        const EsheepPetCatalogEntry *entry = esheep_pet_catalog_get_by_index(catalog, i);
        gboolean avail = esheep_pet_catalog_entry_is_available(entry);
        assert(avail == TRUE);
    }
    
    esheep_pet_catalog_free(catalog);
    printf("OK\n");
}

static void test_catalog_accessors(void) {
    printf("test: catalog accessors\n");
    GError *error = NULL;
    EsheepPetCatalog *catalog = esheep_pet_catalog_load("manifest.json", &error);
    
    const EsheepPetCatalogEntry *entry = esheep_pet_catalog_lookup(catalog, "blue_sheep");
    assert(entry != NULL);
    
    assert(strcmp(esheep_pet_catalog_entry_stable_name(entry), "blue_sheep") == 0);
    assert(strcmp(esheep_pet_catalog_entry_title(entry), "gSheep Blue") == 0);
    assert(strcmp(esheep_pet_catalog_entry_folder(entry), "blue_sheep") == 0);
    assert(strcmp(esheep_pet_catalog_entry_petname(entry), "Ben") == 0);
    assert(strcmp(esheep_pet_catalog_entry_author(entry), "Oliver B.") == 0);
    assert(strcmp(esheep_pet_catalog_entry_version(entry), "8.0") == 0);
    assert(esheep_pet_catalog_entry_tiles_x(entry) == 16);
    assert(esheep_pet_catalog_entry_tiles_y(entry) == 19);
    assert(esheep_pet_catalog_entry_animations_count(entry) == 268);
    assert(esheep_pet_catalog_entry_transitions_count(entry) == 1055);
    assert(esheep_pet_catalog_entry_child_count(entry) == 31);
    assert(esheep_pet_catalog_entry_spawn_count(entry) == 11);
    assert(esheep_pet_catalog_entry_sound_count(entry) == 35);
    assert(esheep_pet_catalog_entry_has_sounds(entry) == TRUE);
    assert(strcmp(esheep_pet_catalog_entry_transparency(entry), "Magenta") == 0);
    assert(strcmp(esheep_pet_catalog_entry_license_status(entry), "unresolved") == 0);
    assert(strcmp(esheep_pet_catalog_entry_attribution(entry), "Upstream contributor") == 0);
    assert(strcmp(esheep_pet_catalog_entry_upstream_revision(entry), "48ee8022c6b0363c79213e06e4bae608a3bdc332") == 0);
    assert(strcmp(esheep_pet_catalog_entry_xml_sha256(entry), "95b6384fe09322e51993c81cee42f1f94358141f494139e3cbd0cb782265cb41") == 0);
    assert(esheep_pet_catalog_entry_has_embedded_image(entry) == TRUE);
    assert(esheep_pet_catalog_entry_has_file_ref(entry) == FALSE);
    assert(esheep_pet_catalog_entry_has_spritesheet_ref(entry) == FALSE);
    assert(esheep_pet_catalog_entry_alias_count(entry) == 3);
    assert(strcmp(esheep_pet_catalog_entry_aliases(entry)[0], "blue sheep") == 0);
    assert(strcmp(esheep_pet_catalog_entry_aliases(entry)[1], "ben") == 0);
    assert(strcmp(esheep_pet_catalog_entry_aliases(entry)[2], "gsheep blue") == 0);
    assert(strcmp(esheep_pet_catalog_entry_package_path(entry), "Pets/blue_sheep/animations.xml") == 0);
    
    esheep_pet_catalog_free(catalog);
    printf("OK\n");
}

static void test_catalog_sheep_variants(void) {
    printf("test: all sheep variants present\n");
    GError *error = NULL;
    EsheepPetCatalog *catalog = esheep_pet_catalog_load("manifest.json", &error);
    
    const char *sheep_folders[] = {
        "blue_sheep", "green_sheep", "orange_sheep",
        "pink_sheep", "purple_sheep", "red_sheep", "yellow_sheep"
    };
    
    for (int i = 0; i < 7; i++) {
        const EsheepPetCatalogEntry *entry = esheep_pet_catalog_lookup_by_folder(catalog, sheep_folders[i]);
        assert(entry != NULL);
        assert(esheep_pet_catalog_entry_animations_count(entry) == 268);
        assert(esheep_pet_catalog_entry_has_sounds(entry) == TRUE);
        assert(esheep_pet_catalog_entry_sound_count(entry) == 35);
    }
    
    esheep_pet_catalog_free(catalog);
    printf("OK\n");
}

int main(void) {
    test_catalog_load();
    test_catalog_lookup_by_stable_name();
    test_catalog_lookup_by_petname();
    test_catalog_lookup_by_title();
    test_catalog_lookup_by_aliases();
    test_catalog_case_insensitive();
    test_catalog_invalid_name();
    test_catalog_lookup_by_folder();
    test_catalog_enumeration();
    test_catalog_availability();
    test_catalog_accessors();
    test_catalog_sheep_variants();
    
    printf("\nAll catalog tests passed!\n");
    return 0;
}
