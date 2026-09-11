#ifndef ESHEEP_AUDIO_CONFIG_H
#define ESHEEP_AUDIO_CONFIG_H

#include <glib.h>

#define ESHEEP_AUDIO_MIN_VOLUME 0
#define ESHEEP_AUDIO_MAX_VOLUME 100
#define ESHEEP_AUDIO_MIN_VOICES 1
#define ESHEEP_AUDIO_MAX_VOICES 32

typedef struct {
    gboolean enabled;
    guint volume;
    guint max_voices;
} EsheepAudioConfig;

/* Configuration precedence is CLI > environment > config file > defaults. */

void esheep_audio_config_defaults(EsheepAudioConfig *config);
void esheep_audio_config_apply_environment(EsheepAudioConfig *config);
void esheep_audio_config_apply_key_file(EsheepAudioConfig *config,
                                         GKeyFile *key_file);
void esheep_audio_config_apply_cli(EsheepAudioConfig *config,
                                   gboolean enabled_set, gboolean enabled,
                                   gboolean volume_set, guint volume,
                                   gboolean voices_set, guint max_voices);

gboolean esheep_audio_config_parse_volume(const char *value, guint *out);
gboolean esheep_audio_config_parse_max_voices(const char *value, guint *out);

#endif
