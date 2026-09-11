#include "esheep_sound_cache.h"
#include <stdlib.h>
#include <string.h>

struct EsheepSoundCache {
    EsheepAudio *audio;
    GArray *entries;
    guint32 rng_state;
};

static int sound_rng_next(EsheepSoundCache *cache) {
    cache->rng_state = cache->rng_state * 1664525u + 1013904223u;
    return (int)((cache->rng_state >> 8) % 100u);
}

EsheepSoundCache *esheep_sound_cache_new(const EsheepPetPackage *package,
                                          EsheepAudio *audio,
                                          GError **error) {
    if (!package || esheep_pet_package_sound_count(package) == 0) {
        if (error) g_set_error(error, G_MARKUP_ERROR, 1, "no sounds in package");
        return NULL;
    }
    if (!audio) {
        if (error) g_set_error(error, G_MARKUP_ERROR, 2, "audio backend not initialized");
        return NULL;
    }

    EsheepSoundCache *cache = g_new0(EsheepSoundCache, 1);
    cache->audio = audio;
    cache->entries = g_array_new(FALSE, FALSE, sizeof(EsheepSoundEntry));
    cache->rng_state = 0x9E3779B9u;

    const EsheepPackageSound *const *sounds = esheep_pet_package_sounds(package);
    int sound_count = esheep_pet_package_sound_count(package);

    for (int i = 0; i < sound_count; i++) {
        const EsheepPackageSound *src = sounds[i];
        if (!src || !src->payload || src->payload_size == 0) continue;

        guchar *payload_copy = g_malloc(src->payload_size);
        memcpy(payload_copy, src->payload, src->payload_size);

        EsheepSoundEntry entry = {
            .animation_id = src->animation_id,
            .probability = src->probability,
            .loop_count = src->loop_count,
            .payload = payload_copy,
            .payload_size = src->payload_size,
            .source_order = i,
        };

        g_array_append_val(cache->entries, entry);
    }

    if (cache->entries->len == 0) {
        esheep_sound_cache_free(cache);
        if (error) g_set_error(error, G_MARKUP_ERROR, 3, "no valid sound payloads");
        return NULL;
    }

    return cache;
}

void esheep_sound_cache_free(EsheepSoundCache *cache) {
    if (!cache) return;
    for (guint i = 0; i < cache->entries->len; i++) {
        EsheepSoundEntry *entry = &g_array_index(cache->entries, EsheepSoundEntry, i);
        g_free(entry->payload);
    }
    g_array_free(cache->entries, TRUE);
    g_free(cache);
}

guint32 esheep_sound_cache_get_rng_state(const EsheepSoundCache *cache) {
    return cache ? cache->rng_state : 0;
}

void esheep_sound_cache_set_rng_seed(EsheepSoundCache *cache, guint32 seed) {
    if (cache) cache->rng_state = seed;
}

int esheep_sound_cache_query(const EsheepSoundCache *cache,
                             int animation_id,
                             int frame_index G_GNUC_UNUSED,
                             const EsheepSoundEntry **entries_out,
                             int capacity) {
    if (!cache) return 0;
    int count = 0;
    for (guint i = 0; i < cache->entries->len; i++) {
        const EsheepSoundEntry *entry = &g_array_index(cache->entries, EsheepSoundEntry, i);
        if (entry->animation_id == animation_id) {
            if (entries_out && count < capacity) entries_out[count] = entry;
            count++;
        }
    }
    return count;
}

int esheep_sound_cache_trigger(EsheepSoundCache *cache,
                               int animation_id,
                               int frame_index G_GNUC_UNUSED) {
    if (!cache || cache->entries->len == 0) return 0;
    int triggered = 0;
    for (guint i = 0; i < cache->entries->len; i++) {
        EsheepSoundEntry *entry = &g_array_index(cache->entries, EsheepSoundEntry, i);
        if (entry->animation_id != animation_id) continue;
        int roll = sound_rng_next(cache);
        if (roll < entry->probability) {
            GError *error = NULL;
            EsheepAudioVoice *voice = esheep_audio_play_mp3(cache->audio,
                                                            entry->payload,
                                                            entry->payload_size,
                                                            &error);
            if (voice) triggered++;
            else if (error) {
                g_warning("Sound playback failed for animation %d: %s",
                          animation_id, error->message);
                g_clear_error(&error);
            }
        }
    }
    return triggered;
}

gboolean esheep_sound_cache_has_sounds(const EsheepSoundCache *cache) {
    return cache && cache->entries->len > 0;
}

int esheep_sound_cache_entry_count(const EsheepSoundCache *cache) {
    return cache ? (int)cache->entries->len : 0;
}
