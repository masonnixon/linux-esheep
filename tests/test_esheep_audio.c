#include <assert.h>
#include <stdio.h>
#include "../src/esheep_audio.h"

int main(void) {
    GError *error = NULL;
    EsheepAudioInitParams params = { .max_voices = 4, .app_name = "test" };
    EsheepAudio *audio = esheep_audio_init(&params, &error);
    assert(audio != NULL && error == NULL);
    assert(esheep_audio_is_noop(audio));
    assert(esheep_audio_get_caps(audio) == ESHEEP_AUDIO_CAP_NONE);
    assert(esheep_audio_max_voices(audio) == 4);
    assert(esheep_audio_active_voices(audio) == 0);

    assert(esheep_audio_play_mp3(audio, NULL, 0, &error) == NULL);
    assert(error != NULL);
    g_clear_error(&error);
    const guchar payload[] = { 0x01, 0x02 };
    assert(esheep_audio_play_mp3(audio, payload, sizeof(payload), &error) == NULL);
    assert(error == NULL); /* silent fallback is not a runtime failure */
    esheep_audio_shutdown(audio);
    puts("All audio abstraction tests passed");
    return 0;
}
