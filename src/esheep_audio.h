#ifndef ESHEEP_AUDIO_H
#define ESHEEP_AUDIO_H

#include <glib.h>

typedef struct EsheepAudio EsheepAudio;
typedef struct EsheepAudioVoice EsheepAudioVoice;

typedef enum {
    ESHEEP_AUDIO_CAP_NONE = 0,
} EsheepAudioCaps;

typedef struct {
    int max_voices;
    gboolean enabled;
    int volume;
    const char *app_name;
} EsheepAudioInitParams;

/* Initializes the optional backend. With the current build dependencies this
 * is an explicit silent backend; initialization succeeds without an error. */
EsheepAudio *esheep_audio_init(const EsheepAudioInitParams *params,
                               GError **error);
void esheep_audio_shutdown(EsheepAudio *audio);
EsheepAudioCaps esheep_audio_get_caps(const EsheepAudio *audio);
gboolean esheep_audio_is_noop(const EsheepAudio *audio);

/* Payload validation is performed even in silent mode so malformed package
 * data cannot be hidden by a capability fallback. */
EsheepAudioVoice *esheep_audio_play_mp3(EsheepAudio *audio,
                                        const guchar *payload,
                                        gsize payload_size,
                                        GError **error);
void esheep_audio_cancel(EsheepAudioVoice *voice);
gboolean esheep_audio_wait(EsheepAudioVoice *voice);
int esheep_audio_active_voices(const EsheepAudio *audio);
int esheep_audio_max_voices(const EsheepAudio *audio);
int esheep_audio_volume(const EsheepAudio *audio);
gboolean esheep_audio_enabled(const EsheepAudio *audio);
void esheep_audio_set_enabled(EsheepAudio *audio, gboolean enabled);
void esheep_audio_set_volume(EsheepAudio *audio, int volume);
void esheep_audio_set_max_voices(EsheepAudio *audio, int max_voices);

#endif
