#include "esheep_audio_config.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static gboolean parse_bounded_uint(const char *value, guint minimum,
                                   guint maximum, guint *out) {
    char *end = NULL;
    unsigned long parsed;
    if (!value || !*value) return FALSE;
    errno = 0;
    parsed = strtoul(value, &end, 10);
    if (errno == ERANGE || *end != '\0' || parsed < minimum || parsed > maximum)
        return FALSE;
    if (out) *out = (guint)parsed;
    return TRUE;
}

static gboolean parse_bool(const char *value, gboolean *out) {
    if (!value) return FALSE;
    if (strcmp(value, "1") == 0 || g_ascii_strcasecmp(value, "true") == 0 ||
        g_ascii_strcasecmp(value, "yes") == 0 ||
        g_ascii_strcasecmp(value, "on") == 0) {
        if (out) *out = TRUE;
        return TRUE;
    }
    if (strcmp(value, "0") == 0 || g_ascii_strcasecmp(value, "false") == 0 ||
        g_ascii_strcasecmp(value, "no") == 0 ||
        g_ascii_strcasecmp(value, "off") == 0) {
        if (out) *out = FALSE;
        return TRUE;
    }
    return FALSE;
}

void esheep_audio_config_defaults(EsheepAudioConfig *config) {
    if (!config) return;
    config->enabled = FALSE;
    config->volume = 100;
    config->max_voices = 8;
}

gboolean esheep_audio_config_parse_volume(const char *value, guint *out) {
    return parse_bounded_uint(value, ESHEEP_AUDIO_MIN_VOLUME,
                              ESHEEP_AUDIO_MAX_VOLUME, out);
}

gboolean esheep_audio_config_parse_max_voices(const char *value, guint *out) {
    return parse_bounded_uint(value, ESHEEP_AUDIO_MIN_VOICES,
                              ESHEEP_AUDIO_MAX_VOICES, out);
}

void esheep_audio_config_apply_environment(EsheepAudioConfig *config) {
    const char *value;
    gboolean enabled;
    guint parsed;
    if (!config) return;
    value = g_getenv("ESHEEP_AUDIO");
    if (parse_bool(value, &enabled)) config->enabled = enabled;
    value = g_getenv("ESHEEP_MASTER_VOLUME");
    if (esheep_audio_config_parse_volume(value, &parsed)) config->volume = parsed;
    value = g_getenv("ESHEEP_MAX_VOICES");
    if (esheep_audio_config_parse_max_voices(value, &parsed)) config->max_voices = parsed;
}

void esheep_audio_config_apply_key_file(EsheepAudioConfig *config,
                                         GKeyFile *key_file) {
    GError *error = NULL;
    gint64 value;
    if (!config || !key_file) return;
    /* Call this before environment application: config is the lower layer. */
    if (g_key_file_has_key(key_file, "esheep", "audio_enabled", NULL)) {
        gboolean enabled = g_key_file_get_boolean(key_file, "esheep",
                                                   "audio_enabled", &error);
        if (!error) config->enabled = enabled;
        g_clear_error(&error);
    }
    if (g_key_file_has_key(key_file, "esheep", "master_volume", NULL)) {
        value = g_key_file_get_int64(key_file, "esheep", "master_volume", &error);
        if (!error && value >= ESHEEP_AUDIO_MIN_VOLUME &&
            value <= ESHEEP_AUDIO_MAX_VOLUME) config->volume = (guint)value;
        g_clear_error(&error);
    }
    if (g_key_file_has_key(key_file, "esheep", "max_voices", NULL)) {
        value = g_key_file_get_int64(key_file, "esheep", "max_voices", &error);
        if (!error && value >= ESHEEP_AUDIO_MIN_VOICES &&
            value <= ESHEEP_AUDIO_MAX_VOICES) config->max_voices = (guint)value;
        g_clear_error(&error);
    }
}

void esheep_audio_config_apply_cli(EsheepAudioConfig *config,
                                   gboolean enabled_set, gboolean enabled,
                                   gboolean volume_set, guint volume,
                                   gboolean voices_set, guint max_voices) {
    if (!config) return;
    if (enabled_set) config->enabled = enabled;
    if (volume_set && volume <= ESHEEP_AUDIO_MAX_VOLUME) config->volume = volume;
    if (voices_set && max_voices >= ESHEEP_AUDIO_MIN_VOICES &&
        max_voices <= ESHEEP_AUDIO_MAX_VOICES) config->max_voices = max_voices;
}
