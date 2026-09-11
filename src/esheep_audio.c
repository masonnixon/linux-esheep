#include "esheep_audio.h"

struct EsheepAudio {
    int max_voices;
    int volume;
    gboolean enabled;
};

struct EsheepAudioVoice {
    gboolean completed;
};

static GQuark audio_error_quark(void) {
    return g_quark_from_static_string("esheep-audio-error");
}

EsheepAudio *esheep_audio_init(const EsheepAudioInitParams *params,
                               GError **error) {
    (void)error;
    EsheepAudio *audio = g_new0(EsheepAudio, 1);
    audio->max_voices = params && params->max_voices > 0 ? params->max_voices : 8;
    audio->volume = params && params->volume >= 0 ? params->volume : 100;
    if (audio->volume > 100) audio->volume = 100;
    audio->enabled = !params || params->enabled;
    return audio;
}

void esheep_audio_shutdown(EsheepAudio *audio) {
    g_free(audio);
}

EsheepAudioCaps esheep_audio_get_caps(const EsheepAudio *audio) {
    (void)audio;
    return ESHEEP_AUDIO_CAP_NONE;
}

gboolean esheep_audio_is_noop(const EsheepAudio *audio) {
    (void)audio;
    return TRUE;
}

EsheepAudioVoice *esheep_audio_play_mp3(EsheepAudio *audio,
                                        const guchar *payload,
                                        gsize payload_size,
                                        GError **error) {
    if (!audio) {
        g_set_error(error, audio_error_quark(), 1, "audio backend is not initialized");
        return NULL;
    }
    if (!payload || payload_size == 0) {
        g_set_error(error, audio_error_quark(), 2, "audio payload is empty");
        return NULL;
    }
    if (!audio->enabled) return NULL;
    /* No backend is available in this build. Returning NULL without an error
     * is the documented silent fallback. */
    return NULL;
}

void esheep_audio_cancel(EsheepAudioVoice *voice) {
    if (voice) voice->completed = TRUE;
}

gboolean esheep_audio_wait(EsheepAudioVoice *voice) {
    if (!voice) return FALSE;
    gboolean completed = voice->completed;
    g_free(voice);
    return completed;
}

int esheep_audio_active_voices(const EsheepAudio *audio) {
    (void)audio;
    return 0;
}

int esheep_audio_max_voices(const EsheepAudio *audio) {
    return audio ? audio->max_voices : 0;
}

int esheep_audio_volume(const EsheepAudio *audio) {
    return audio ? audio->volume : 0;
}

gboolean esheep_audio_enabled(const EsheepAudio *audio) {
    return audio && audio->enabled;
}

void esheep_audio_set_enabled(EsheepAudio *audio, gboolean enabled) {
    if (audio) audio->enabled = enabled;
}

void esheep_audio_set_volume(EsheepAudio *audio, int volume) {
    if (!audio) return;
    audio->volume = CLAMP(volume, 0, 100);
}

void esheep_audio_set_max_voices(EsheepAudio *audio, int max_voices) {
    if (!audio) return;
    audio->max_voices = CLAMP(max_voices, 1, 32);
}
