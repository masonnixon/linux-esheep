#define _POSIX_C_SOURCE 200809L

#include "esheep_audio.h"

#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

struct EsheepAudio {
    int max_voices;
    int volume;
    gboolean enabled;
    char *player;
    GList *voices;
    GMutex mutex;
};

struct EsheepAudioVoice {
    EsheepAudio *audio;
    pid_t pid;
    char *path;
    gboolean completed;
};

static GQuark audio_error_quark(void) {
    return g_quark_from_static_string("esheep-audio-error");
}

static gboolean mp3_payload_looks_valid(const guchar *payload, gsize size) {
    if (size >= 10 && memcmp(payload, "ID3", 3) == 0) {
        for (gsize i = 6; i < 10; i++) if (payload[i] & 0x80) return FALSE;
        return TRUE;
    }
    if (size < 4) return FALSE;
    guint32 header = ((guint32)payload[0] << 24) | ((guint32)payload[1] << 16) |
                     ((guint32)payload[2] << 8) | payload[3];
    guint bitrate = (header >> 12) & 0xf;
    guint sample_rate = (header >> 10) & 0x3;
    guint layer = (header >> 17) & 0x3;
    return (header & 0xffe00000u) == 0xffe00000u && layer && bitrate &&
           bitrate != 15 && sample_rate != 3;
}

static void voice_cleanup(EsheepAudioVoice *voice) {
    unlink(voice->path);
    g_free(voice->path);
    g_free(voice);
}

static void reap_finished_locked(EsheepAudio *audio) {
    for (GList *node = audio->voices; node; node = node->next) {
        EsheepAudioVoice *voice = node->data;
        if (!voice->completed &&
            (errno = 0, waitpid(voice->pid, NULL, WNOHANG) == voice->pid || errno == ECHILD))
            voice->completed = TRUE;
    }
}

EsheepAudio *esheep_audio_init(const EsheepAudioInitParams *params,
                               GError **error) {
    (void)error;
    EsheepAudio *audio = g_new0(EsheepAudio, 1);
    audio->max_voices = params && params->max_voices > 0 ? params->max_voices : 8;
    audio->volume = params && params->volume >= 0 ? params->volume : 100;
    if (audio->volume > 100) audio->volume = 100;
    audio->enabled = !params || params->enabled;
    g_mutex_init(&audio->mutex);
    const char *requested = g_getenv("ESHEEP_AUDIO_PLAYER");
    audio->player = g_find_program_in_path(requested && *requested ? requested : "ffplay");
    return audio;
}

void esheep_audio_shutdown(EsheepAudio *audio) {
    if (!audio) return;
    g_mutex_lock(&audio->mutex);
    for (GList *node = audio->voices; node; node = node->next) {
        EsheepAudioVoice *voice = node->data;
        if (!voice->completed) {
            kill(voice->pid, SIGKILL);
            while (waitpid(voice->pid, NULL, 0) < 0 && errno == EINTR) {}
        }
        voice_cleanup(voice);
    }
    g_list_free(audio->voices);
    g_mutex_unlock(&audio->mutex);
    g_mutex_clear(&audio->mutex);
    g_free(audio->player);
    g_free(audio);
}

EsheepAudioCaps esheep_audio_get_caps(const EsheepAudio *audio) {
    return audio && audio->player ? ESHEEP_AUDIO_CAP_MP3 : ESHEEP_AUDIO_CAP_NONE;
}

gboolean esheep_audio_is_noop(const EsheepAudio *audio) {
    return !audio || !audio->player;
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
    if (!mp3_payload_looks_valid(payload, payload_size)) {
        g_set_error(error, audio_error_quark(), 3, "audio payload is not a valid MP3");
        return NULL;
    }
    if (!audio->enabled) return NULL;
    if (!audio->player || audio->volume == 0) return NULL;

    g_mutex_lock(&audio->mutex);
    reap_finished_locked(audio);
    int active = 0;
    for (GList *node = audio->voices; node; node = node->next)
        if (!((EsheepAudioVoice *)node->data)->completed) active++;
    if (active >= audio->max_voices) {
        g_mutex_unlock(&audio->mutex);
        return NULL;
    }
    char *path = NULL;
    GError *file_error = NULL;
    int fd = g_file_open_tmp("esheep-audio-XXXXXX.mp3", &path, &file_error);
    if (fd < 0 || !g_file_set_contents(path, (const gchar *)payload, payload_size, &file_error)) {
        if (fd >= 0) close(fd);
        if (path) unlink(path);
        g_free(path);
        g_clear_error(&file_error);
        g_mutex_unlock(&audio->mutex);
        return NULL;
    }
    close(fd);
    pid_t pid = fork();
    if (pid == 0) {
        char volume[4];
        g_snprintf(volume, sizeof volume, "%d", audio->volume);
        char *const argv[] = { audio->player, "-nodisp", "-autoexit", "-loglevel", "quiet",
                               "-volume", volume, path, NULL };
        execv(audio->player, argv);
        _exit(127);
    }
    if (pid < 0) {
        unlink(path); g_free(path); g_mutex_unlock(&audio->mutex); return NULL;
    }
    EsheepAudioVoice *voice = g_new0(EsheepAudioVoice, 1);
    voice->audio = audio; voice->pid = pid; voice->path = path;
    audio->voices = g_list_prepend(audio->voices, voice);
    g_mutex_unlock(&audio->mutex);
    return voice;
}

void esheep_audio_cancel(EsheepAudioVoice *voice) {
    if (!voice) return;
    g_mutex_lock(&voice->audio->mutex);
    if (!voice->completed) {
        kill(voice->pid, SIGKILL);
        while (waitpid(voice->pid, NULL, 0) < 0 && errno == EINTR) {}
        voice->completed = TRUE;
    }
    g_mutex_unlock(&voice->audio->mutex);
}

gboolean esheep_audio_wait(EsheepAudioVoice *voice) {
    if (!voice) return FALSE;
    EsheepAudio *audio = voice->audio;
    g_mutex_lock(&audio->mutex);
    while (!voice->completed) {
        pid_t result = waitpid(voice->pid, NULL, 0);
        if (result == voice->pid || (result < 0 && errno == ECHILD)) voice->completed = TRUE;
        else if (result < 0 && errno != EINTR) voice->completed = TRUE;
    }
    gboolean completed = voice->completed;
    audio->voices = g_list_remove(audio->voices, voice);
    voice_cleanup(voice);
    g_mutex_unlock(&audio->mutex);
    return completed;
}

int esheep_audio_active_voices(const EsheepAudio *audio) {
    if (!audio) return 0;
    g_mutex_lock((GMutex *)&audio->mutex);
    reap_finished_locked((EsheepAudio *)audio);
    int active = 0;
    for (GList *node = audio->voices; node; node = node->next)
        if (!((EsheepAudioVoice *)node->data)->completed) active++;
    g_mutex_unlock((GMutex *)&audio->mutex);
    return active;
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
