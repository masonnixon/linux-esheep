#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../src/esheep_audio.h"
#include "../src/esheep_audio_config.h"

static void test_precedence_and_bounds(void) {
    GKeyFile *key_file = g_key_file_new();
    EsheepAudioConfig config;
    guint value;
    esheep_audio_config_defaults(&config);
    g_key_file_set_boolean(key_file, "esheep", "audio_enabled", FALSE);
    g_key_file_set_integer(key_file, "esheep", "master_volume", 20);
    g_key_file_set_integer(key_file, "esheep", "max_voices", 3);
    esheep_audio_config_apply_key_file(&config, key_file);
    assert(!config.enabled && config.volume == 20 && config.max_voices == 3);
    setenv("ESHEEP_AUDIO", "true", 1);
    setenv("ESHEEP_MASTER_VOLUME", "40", 1);
    setenv("ESHEEP_MAX_VOICES", "5", 1);
    esheep_audio_config_apply_environment(&config);
    esheep_audio_config_apply_cli(&config, TRUE, FALSE, TRUE, 60, TRUE, 7);
    assert(!config.enabled && config.volume == 60 && config.max_voices == 7);
    assert(!esheep_audio_config_parse_volume("101", &value));
    assert(!esheep_audio_config_parse_max_voices("0", &value));
    assert(esheep_audio_config_parse_volume("0", &value) && value == 0);
    assert(esheep_audio_config_parse_max_voices("32", &value) && value == 32);
    unsetenv("ESHEEP_AUDIO"); unsetenv("ESHEEP_MASTER_VOLUME"); unsetenv("ESHEEP_MAX_VOICES");
    g_key_file_free(key_file);
}

static void test_runtime_toggle_and_limits(void) {
    EsheepAudioInitParams params = { .max_voices = 4, .enabled = TRUE, .volume = 80 };
    EsheepAudio *audio = esheep_audio_init(&params, NULL);
    assert(esheep_audio_enabled(audio) && esheep_audio_volume(audio) == 80);
    esheep_audio_set_enabled(audio, FALSE);
    assert(!esheep_audio_enabled(audio));
    esheep_audio_set_volume(audio, 120);
    esheep_audio_set_max_voices(audio, 100);
    assert(esheep_audio_volume(audio) == 100 && esheep_audio_max_voices(audio) == 32);
    esheep_audio_shutdown(audio);
}

int main(void) {
    test_precedence_and_bounds();
    test_runtime_toggle_and_limits();
    puts("All audio control tests passed");
    return 0;
}
