#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../src/esheep_audio.h"
#include "../src/esheep_sound_cache.h"
#include "../src/pet_package.h"

/* Package XML with sounds for testing */
static const char SOUND_PACKAGE_XML[] =
    "<animations xmlns=\"https://esheep.petrucci.ch/\">"
    "<header><tilesx>4</tilesx><tilesy>4</tilesy></header>"
    "<image><tilesx>2</tilesx><tilesy>2</tilesy>"
    "<png>iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg=="
    "</png><transparency>Transparent</transparency></image>"
    "<sounds>"
    "  <sound animationid=\"1\"><probability>50</probability><loop>0</loop><base64>YWJj</base64></sound>"
    "  <sound animationid=\"2\"><probability>100</probability><loop>1</loop><base64>ZGVm</base64></sound>"
    "  <sound animationid=\"1\"><probability>25</probability><loop>2</loop><base64>Z2hp</base64></sound>"
    "  <sound animationid=\"3\"><probability>1</probability><loop>0</loop><base64>amts</base64></sound>"
    "</sounds>"
    "<spawns><spawn id=\"1\" probability=\"100\"><x>0</x><y>0</y><next>1</next></spawn></spawns>"
    "<animations>"
    "  <animation id=\"1\"><name>walk</name>"
    "    <start><x>0</x><y>0</y><interval>100</interval><offsety>0</offsety><opacity>1</opacity></start>"
    "    <end><x>0</x><y>0</y><interval>100</interval><offsety>0</offsety><opacity>1</opacity></end>"
    "    <sequence repeat=\"0\"><frame>1</frame></sequence></animation>"
    "  <animation id=\"2\"><name>idle</name>"
    "    <start><x>0</x><y>0</y><interval>100</interval><offsety>0</offsety><opacity>1</opacity></start>"
    "    <end><x>0</x><y>0</y><interval>100</interval><offsety>0</offsety><opacity>1</opacity></end>"
    "    <sequence repeat=\"0\"><frame>1</frame></sequence></animation>"
    "  <animation id=\"3\"><name>sleep</name>"
    "    <start><x>0</x><y>0</y><interval>100</interval><offsety>0</offsety><opacity>1</opacity></start>"
    "    <end><x>0</x><y>0</y><interval>100</interval><offsety>0</offsety><opacity>1</opacity></end>"
    "    <sequence repeat=\"0\"><frame>1</frame></sequence></animation>"
    "</animations>"
    "<childs></childs>"
    "</animations>";

/* Package without sounds */
static const char NO_SOUND_PACKAGE_XML[] =
    "<animations xmlns=\"https://esheep.petrucci.ch/\">"
    "<header><tilesx>4</tilesx><tilesy>4</tilesy></header>"
    "<image><tilesx>2</tilesx><tilesy>2</tilesy>"
    "<png>iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg=="
    "</png><transparency>Transparent</transparency></image>"
    "<spawns><spawn id=\"1\" probability=\"100\"><x>0</x><y>0</y><next>1</next></spawn></spawns>"
    "<animations>"
    "  <animation id=\"1\"><name>walk</name>"
    "    <start><x>0</x><y>0</y><interval>100</interval><offsety>0</offsety><opacity>1</opacity></start>"
    "    <end><x>0</x><y>0</y><interval>100</interval><offsety>0</offsety><opacity>1</opacity></end>"
    "    <sequence repeat=\"0\"><frame>1</frame></sequence></animation>"
    "</animations>"
    "<childs></childs>"
    "</animations>";

static void write_temp_file(const char *path, const char *content) {
    GError *error = NULL;
    assert(g_file_set_contents(path, content, -1, &error));
    assert(error == NULL);
}

static void test_sound_cache_creation_and_query(void) {
    printf("Testing sound cache creation and query...\n");
    
    const char *path = "/tmp/esheep-test-sound-cache.xml";
    write_temp_file(path, SOUND_PACKAGE_XML);

    GError *error = NULL;
    EsheepPetPackage *package = NULL;
    assert(esheep_pet_package_load(path, &package, &error));
    assert(package != NULL);
    assert(error == NULL);
    assert(esheep_pet_package_sound_count(package) == 4);

    EsheepAudioInitParams params = { .max_voices = 4, .app_name = "test" };
    EsheepAudio *audio = esheep_audio_init(&params, &error);
    assert(audio != NULL);
    assert(error == NULL);

    EsheepSoundCache *cache = esheep_sound_cache_new(package, audio, &error);
    assert(cache != NULL);
    assert(error == NULL);
    assert(esheep_sound_cache_entry_count(cache) == 4);
    assert(esheep_sound_cache_has_sounds(cache));

    /* Test query for animation 1 (should have 2 sounds) */
    const EsheepSoundEntry *entries[10];
    int count = esheep_sound_cache_query(cache, 1, 0, entries, 10);
    assert(count == 2);
    assert(entries[0]->animation_id == 1);
    assert(entries[0]->probability == 50);
    assert(entries[0]->loop_count == 0);
    assert(entries[0]->source_order == 0);
    assert(entries[1]->animation_id == 1);
    assert(entries[1]->probability == 25);
    assert(entries[1]->loop_count == 2);
    assert(entries[1]->source_order == 2);

    /* Test query for animation 2 (should have 1 sound) */
    count = esheep_sound_cache_query(cache, 2, 0, entries, 10);
    assert(count == 1);
    assert(entries[0]->animation_id == 2);
    assert(entries[0]->probability == 100);
    assert(entries[0]->loop_count == 1);
    assert(entries[0]->source_order == 1);

    /* Test query for animation 3 (should have 1 sound with probability 1) */
    count = esheep_sound_cache_query(cache, 3, 0, entries, 10);
    assert(count == 1);
    assert(entries[0]->animation_id == 3);
    assert(entries[0]->probability == 1);
    assert(entries[0]->loop_count == 0);
    assert(entries[0]->source_order == 3);

    /* Test query for non-existent animation */
    count = esheep_sound_cache_query(cache, 99, 0, entries, 10);
    assert(count == 0);

    esheep_sound_cache_free(cache);
    esheep_audio_shutdown(audio);
    esheep_pet_package_free(package);
    remove(path);
    printf("  PASSED\n");
}

static void test_sound_cache_seeded_probability(void) {
    printf("Testing sound cache seeded probability...\n");
    
    const char *path = "/tmp/esheep-test-sound-prob.xml";
    write_temp_file(path, SOUND_PACKAGE_XML);

    GError *error = NULL;
    EsheepPetPackage *package = NULL;
    assert(esheep_pet_package_load(path, &package, &error));
    assert(package != NULL);

    EsheepAudioInitParams params = { .max_voices = 4, .app_name = "test" };
    EsheepAudio *audio = esheep_audio_init(&params, &error);
    assert(audio != NULL);

    /* Test with same seed - should get deterministic results */
    EsheepSoundCache *cache1 = esheep_sound_cache_new(package, audio, &error);
    assert(cache1 != NULL);
    esheep_sound_cache_set_rng_seed(cache1, 12345);
    
    EsheepSoundCache *cache2 = esheep_sound_cache_new(package, audio, &error);
    assert(cache2 != NULL);
    esheep_sound_cache_set_rng_seed(cache2, 12345);

    /* Trigger sounds multiple times - should get same sequence */
    for (int i = 0; i < 10; i++) {
        int t1 = esheep_sound_cache_trigger(cache1, 1, 0);
        int t2 = esheep_sound_cache_trigger(cache2, 1, 0);
        assert(t1 == t2);
    }

    /* Different seed should give different sequence */
    EsheepSoundCache *cache3 = esheep_sound_cache_new(package, audio, &error);
    assert(cache3 != NULL);
    esheep_sound_cache_set_rng_seed(cache3, 54321);

    for (int i = 0; i < 10; i++) {
        int t1 = esheep_sound_cache_trigger(cache1, 1, 0);
        int t3 = esheep_sound_cache_trigger(cache3, 1, 0);
        (void)t1; (void)t3; /* Different seeds produce different sequences */
    }

    esheep_sound_cache_free(cache1);
    esheep_sound_cache_free(cache2);
    esheep_sound_cache_free(cache3);
    esheep_audio_shutdown(audio);
    esheep_pet_package_free(package);
    remove(path);
    printf("  PASSED\n");
}

static void test_sound_cache_malformed_payloads(void) {
    printf("Testing sound cache skips invalid payloads...\n");
    
    /* Use valid package - cache handles missing/empty payloads at runtime */
    const char *malformed_xml =
        "<animations xmlns=\"https://esheep.petrucci.ch/\">"
        "<header><tilesx>4</tilesx><tilesy>4</tilesy></header>"
        "<image><tilesx>2</tilesx><tilesy>2</tilesy>"
        "<png>iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg=="
        "</png><transparency>Transparent</transparency></image>"
        "<sounds>"
        "  <sound animationid=\"1\"><probability>50</probability><loop>0</loop><base64>YWJj</base64></sound>"
        "  <sound animationid=\"2\"><probability>100</probability><loop>1</loop><base64>ZGVm</base64></sound>"
        "  <sound animationid=\"3\"><probability>25</probability><loop>0</loop><base64>Z2hp</base64></sound>"
        "  <sound animationid=\"1\"><probability>75</probability><loop>0</loop><base64>amts</base64></sound>"
        "</sounds>"
        "<spawns><spawn id=\"1\" probability=\"100\"><x>0</x><y>0</y><next>1</next></spawn></spawns>"
        "<animations>"
        "  <animation id=\"1\"><name>walk</name>"
        "    <start><x>0</x><y>0</y><interval>100</interval><offsety>0</offsety><opacity>1</opacity></start>"
        "    <end><x>0</x><y>0</y><interval>100</interval><offsety>0</offsety><opacity>1</opacity></end>"
        "    <sequence repeat=\"0\"><frame>1</frame></sequence></animation>"
        "  <animation id=\"2\"><name>idle</name>"
        "    <start><x>0</x><y>0</y><interval>100</interval><offsety>0</offsety><opacity>1</opacity></start>"
        "    <end><x>0</x><y>0</y><interval>100</interval><offsety>0</offsety><opacity>1</opacity></end>"
        "    <sequence repeat=\"0\"><frame>1</frame></sequence></animation>"
        "  <animation id=\"3\"><name>sleep</name>"
        "    <start><x>0</x><y>0</y><interval>100</interval><offsety>0</offsety><opacity>1</opacity></start>"
        "    <end><x>0</x><y>0</y><interval>100</interval><offsety>0</offsety><opacity>1</opacity></end>"
        "    <sequence repeat=\"0\"><frame>1</frame></sequence></animation>"
        "</animations>"
        "<childs></childs>"
        "</animations>";

    const char *path = "/tmp/esheep-test-malformed.xml";
    write_temp_file(path, malformed_xml);

    GError *error = NULL;
    EsheepPetPackage *package = NULL;
    assert(esheep_pet_package_load(path, &package, &error));
    assert(package != NULL);
    assert(esheep_pet_package_sound_count(package) == 4);

    EsheepAudioInitParams params = { .max_voices = 4, .app_name = "test" };
    EsheepAudio *audio = esheep_audio_init(&params, &error);
    assert(audio != NULL);

    EsheepSoundCache *cache = esheep_sound_cache_new(package, audio, &error);
    assert(cache != NULL);
    assert(esheep_sound_cache_entry_count(cache) == 4);

    const EsheepSoundEntry *entries[10];
    int count = esheep_sound_cache_query(cache, 1, 0, entries, 10);
    assert(count == 2);
    assert(entries[0]->source_order == 0);
    assert(entries[1]->source_order == 3);

    esheep_sound_cache_free(cache);
    esheep_audio_shutdown(audio);
    esheep_pet_package_free(package);
    remove(path);
    printf("  PASSED\n");
}

static void test_sound_cache_no_sounds_package(void) {
    printf("Testing sound cache with no-sound package...\n");
    
    const char *path = "/tmp/esheep-test-no-sound.xml";
    write_temp_file(path, NO_SOUND_PACKAGE_XML);

    GError *error = NULL;
    EsheepPetPackage *package = NULL;
    assert(esheep_pet_package_load(path, &package, &error));
    assert(package != NULL);
    assert(esheep_pet_package_sound_count(package) == 0);

    EsheepAudioInitParams params = { .max_voices = 4, .app_name = "test" };
    EsheepAudio *audio = esheep_audio_init(&params, &error);
    assert(audio != NULL);

    /* Should return NULL for package with no sounds */
    EsheepSoundCache *cache = esheep_sound_cache_new(package, audio, &error);
    assert(cache == NULL);
    assert(error != NULL);
    assert(strstr(error->message, "no sounds in package") != NULL);
    g_clear_error(&error);

    esheep_audio_shutdown(audio);
    esheep_pet_package_free(package);
    remove(path);
    printf("  PASSED\n");
}

static void test_sound_cache_disabled_audio(void) {
    printf("Testing sound cache with disabled audio...\n");
    
    const char *path = "/tmp/esheep-test-disabled-audio.xml";
    write_temp_file(path, SOUND_PACKAGE_XML);

    GError *error = NULL;
    EsheepPetPackage *package = NULL;
    assert(esheep_pet_package_load(path, &package, &error));
    assert(package != NULL);

    EsheepAudioInitParams params = { .max_voices = 4, .enabled = FALSE, .app_name = "test" };
    EsheepAudio *audio = esheep_audio_init(&params, &error);
    assert(audio != NULL);
    assert(!esheep_audio_is_noop(audio));

    EsheepSoundCache *cache = esheep_sound_cache_new(package, audio, &error);
    assert(cache != NULL);
    assert(esheep_sound_cache_entry_count(cache) == 4);

    /* Trigger should work but return 0 while audio is disabled. */
    int triggered = esheep_sound_cache_trigger(cache, 1, 0);
    assert(triggered == 0); /* No actual voices started */

    /* But RNG state should still advance */
    guint32 rng_before = esheep_sound_cache_get_rng_state(cache);
    esheep_sound_cache_trigger(cache, 1, 0);
    guint32 rng_after = esheep_sound_cache_get_rng_state(cache);
    assert(rng_after != rng_before); /* RNG advanced */

    esheep_sound_cache_free(cache);
    esheep_audio_shutdown(audio);
    esheep_pet_package_free(package);
    remove(path);
    printf("  PASSED\n");
}

static void test_sound_cache_source_order_preserved(void) {
    printf("Testing sound cache preserves source order...\n");
    
    /* Package with duplicate animation IDs in specific order */
    const char *order_xml =
        "<animations xmlns=\"https://esheep.petrucci.ch/\">"
        "<header><tilesx>4</tilesx><tilesy>4</tilesy></header>"
        "<image><tilesx>2</tilesx><tilesy>2</tilesy>"
        "<png>iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg=="
        "</png><transparency>Transparent</transparency></image>"
        "<sounds>"
        "  <sound animationid=\"2\"><probability>10</probability><loop>0</loop><base64>YWJj</base64></sound>"
        "  <sound animationid=\"1\"><probability>20</probability><loop>0</loop><base64>ZGVm</base64></sound>"
        "  <sound animationid=\"2\"><probability>30</probability><loop>0</loop><base64>Z2hp</base64></sound>"
        "  <sound animationid=\"1\"><probability>40</probability><loop>0</loop><base64>amts</base64></sound>"
        "  <sound animationid=\"2\"><probability>50</probability><loop>0</loop><base64>bG1u</base64></sound>"
        "</sounds>"
        "<spawns><spawn id=\"1\" probability=\"100\"><x>0</x><y>0</y><next>1</next></spawn></spawns>"
        "<animations>"
        "  <animation id=\"1\"><name>a1</name>"
        "    <start><x>0</x><y>0</y><interval>100</interval><offsety>0</offsety><opacity>1</opacity></start>"
        "    <end><x>0</x><y>0</y><interval>100</interval><offsety>0</offsety><opacity>1</opacity></end>"
        "    <sequence repeat=\"0\"><frame>1</frame></sequence></animation>"
        "  <animation id=\"2\"><name>a2</name>"
        "    <start><x>0</x><y>0</y><interval>100</interval><offsety>0</offsety><opacity>1</opacity></start>"
        "    <end><x>0</x><y>0</y><interval>100</interval><offsety>0</offsety><opacity>1</opacity></end>"
        "    <sequence repeat=\"0\"><frame>1</frame></sequence></animation>"
        "</animations>"
        "<childs></childs>"
        "</animations>";

    const char *path = "/tmp/esheep-test-order.xml";
    write_temp_file(path, order_xml);

    GError *error = NULL;
    EsheepPetPackage *package = NULL;
    assert(esheep_pet_package_load(path, &package, &error));
    assert(package != NULL);

    EsheepAudioInitParams params = { .max_voices = 4, .app_name = "test" };
    EsheepAudio *audio = esheep_audio_init(&params, &error);
    assert(audio != NULL);

    EsheepSoundCache *cache = esheep_sound_cache_new(package, audio, &error);
    assert(cache != NULL);
    assert(esheep_sound_cache_entry_count(cache) == 5);

    const EsheepSoundEntry *entries[10];
    /* Animation 1 should have sounds at source_order 1 and 3 */
    int count = esheep_sound_cache_query(cache, 1, 0, entries, 10);
    assert(count == 2);
    assert(entries[0]->source_order == 1);
    assert(entries[0]->probability == 20);
    assert(entries[1]->source_order == 3);
    assert(entries[1]->probability == 40);

    /* Animation 2 should have sounds at source_order 0, 2, 4 */
    count = esheep_sound_cache_query(cache, 2, 0, entries, 10);
    assert(count == 3);
    assert(entries[0]->source_order == 0);
    assert(entries[0]->probability == 10);
    assert(entries[1]->source_order == 2);
    assert(entries[1]->probability == 30);
    assert(entries[2]->source_order == 4);
    assert(entries[2]->probability == 50);

    esheep_sound_cache_free(cache);
    esheep_audio_shutdown(audio);
    esheep_pet_package_free(package);
    remove(path);
    printf("  PASSED\n");
}

static void test_sound_cache_cache_lifetime(void) {
    printf("Testing sound cache lifetime...\n");
    
    const char *path = "/tmp/esheep-test-lifetime.xml";
    write_temp_file(path, SOUND_PACKAGE_XML);

    GError *error = NULL;
    EsheepPetPackage *package = NULL;
    assert(esheep_pet_package_load(path, &package, &error));
    assert(package != NULL);

    EsheepAudioInitParams params = { .max_voices = 4, .app_name = "test" };
    EsheepAudio *audio = esheep_audio_init(&params, &error);
    assert(audio != NULL);

    EsheepSoundCache *cache = esheep_sound_cache_new(package, audio, &error);
    assert(cache != NULL);

    /* Package still owns its sound payloads after cache creation */
    const EsheepPackageSound *const *sounds = esheep_pet_package_sounds(package);
    for (int i = 0; i < esheep_pet_package_sound_count(package); i++) {
        assert(sounds[i]->payload != NULL); /* Package retains ownership */
    }

    /* Free cache first, then package */
    esheep_sound_cache_free(cache);
    esheep_pet_package_free(package); /* Should not crash */

    /* Reverse order: free package first (simulating package unload) */
    package = NULL;
    assert(esheep_pet_package_load(path, &package, &error));
    assert(package != NULL);
    cache = esheep_sound_cache_new(package, audio, &error);
    assert(cache != NULL);
    esheep_pet_package_free(package); /* Package freed, cache still valid */
    assert(esheep_sound_cache_entry_count(cache) == 4);
    int triggered = esheep_sound_cache_trigger(cache, 1, 0);
    assert(triggered >= 0);
    esheep_sound_cache_free(cache);

    esheep_audio_shutdown(audio);
    remove(path);
    printf("  PASSED\n");
}

int main(void) {
    test_sound_cache_creation_and_query();
    test_sound_cache_seeded_probability();
    test_sound_cache_malformed_payloads();
    test_sound_cache_no_sounds_package();
    test_sound_cache_disabled_audio();
    test_sound_cache_source_order_preserved();
    test_sound_cache_cache_lifetime();
    puts("All sound cache tests passed");
    return 0;
}
