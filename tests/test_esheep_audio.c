#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../src/esheep_audio.h"

int main(void) {
    const char *player = "/tmp/esheep-test-audio-player.sh";
    assert(g_file_set_contents(player, "#!/bin/sh\nexec sleep 1\n", -1, NULL));
    assert(chmod(player, 0700) == 0);
    assert(g_setenv("ESHEEP_AUDIO_PLAYER", player, TRUE));

    GError *error = NULL;
    EsheepAudioInitParams params = { .max_voices = 2, .enabled = TRUE, .volume = 73,
                                     .app_name = "test" };
    EsheepAudio *audio = esheep_audio_init(&params, &error);
    assert(audio != NULL && error == NULL);
    assert(!esheep_audio_is_noop(audio));
    assert(esheep_audio_get_caps(audio) == ESHEEP_AUDIO_CAP_MP3);
    assert(esheep_audio_max_voices(audio) == 2);
    assert(esheep_audio_active_voices(audio) == 0);

    assert(esheep_audio_play_mp3(audio, NULL, 0, &error) == NULL);
    assert(error != NULL);
    g_clear_error(&error);
    const guchar malformed[] = { 0x01, 0x02, 0x03, 0x04 };
    assert(esheep_audio_play_mp3(audio, malformed, sizeof(malformed), &error) == NULL);
    assert(error != NULL);
    g_clear_error(&error);

    const guchar payload[] = { 0xff, 0xfb, 0x90, 0x64 };
    gint64 started = g_get_monotonic_time();
    EsheepAudioVoice *first = esheep_audio_play_mp3(audio, payload, sizeof(payload), &error);
    assert(first != NULL && error == NULL);
    assert(g_get_monotonic_time() - started < 500000);
    EsheepAudioVoice *second = esheep_audio_play_mp3(audio, payload, sizeof(payload), &error);
    assert(second != NULL && esheep_audio_active_voices(audio) == 2);
    assert(esheep_audio_play_mp3(audio, payload, sizeof(payload), &error) == NULL);
    assert(error == NULL);
    esheep_audio_cancel(first);
    assert(esheep_audio_wait(first));
    assert(esheep_audio_active_voices(audio) <= 1);
    assert(esheep_audio_wait(second));
    assert(esheep_audio_active_voices(audio) == 0);

    esheep_audio_set_enabled(audio, FALSE);
    assert(esheep_audio_play_mp3(audio, payload, sizeof(payload), &error) == NULL);
    assert(error == NULL && esheep_audio_active_voices(audio) == 0);

    esheep_audio_set_enabled(audio, TRUE);
    EsheepAudioVoice *cleanup_voice = esheep_audio_play_mp3(audio, payload, sizeof(payload), NULL);
    assert(cleanup_voice != NULL && esheep_audio_active_voices(audio) == 1);
    esheep_audio_shutdown(audio);
    unlink(player);
    g_unsetenv("ESHEEP_AUDIO_PLAYER");
    puts("All audio abstraction tests passed");
    return 0;
}
