#ifndef ESHEEP_SOUND_CACHE_H
#define ESHEEP_SOUND_CACHE_H

#include <glib.h>
#include "pet_package.h"
#include "esheep_audio.h"

typedef struct EsheepSoundCache EsheepSoundCache;
typedef struct EsheepSoundEntry EsheepSoundEntry;

/* Opaque sound entry holding a cached decoded payload and its metadata.
 * The cache owns the entry; do not free individual fields. */
struct EsheepSoundEntry {
    int animation_id;      /* animation this sound is associated with */
    int probability;       /* 0-100 probability from package */
    int loop_count;        /* loop count from package (0 = no loop) */
    guchar *payload;       /* decoded MP3 payload, owned by cache */
    gsize payload_size;    /* size of payload in bytes */
    int source_order;      /* original index in package sounds array */
};

/* Initialize the sound cache with a package's sound records and audio backend.
 * The cache copies the package's sound payloads (the package retains ownership
 * of its original payloads). Returns NULL on error (e.g., no sounds, no audio
 * backend). */
EsheepSoundCache *esheep_sound_cache_new(const EsheepPetPackage *package,
                                          EsheepAudio *audio,
                                          GError **error);

/* Free the sound cache and all owned payloads. */
void esheep_sound_cache_free(EsheepSoundCache *cache);

/* Get the deterministic RNG state for sound selection.
 * This is a separate stream from movement RNG so audio decisions
 * never affect pet movement sequences. */
guint32 esheep_sound_cache_get_rng_state(const EsheepSoundCache *cache);

/* Set the deterministic RNG seed for reproducible sound scheduling.
 * Call once at startup with a fixed seed for deterministic tests. */
void esheep_sound_cache_set_rng_seed(EsheepSoundCache *cache, guint32 seed);

/* Check if any sounds are scheduled for the given animation_id at the
 * given frame_index. Returns the number of matching sound entries.
 * If entries_out is non-NULL and capacity > 0, fills the array
 * with pointers to matching entries (up to capacity). The entries remain
 * owned by the cache. */
int esheep_sound_cache_query(const EsheepSoundCache *cache,
                             int animation_id,
                             int frame_index,
                             const EsheepSoundEntry **entries_out,
                             int capacity);

/* Attempt to play all sounds matching the given animation_id and frame_index.
 * For each matching sound, rolls its own probability using the cache's
 * separate RNG stream. If the roll succeeds, schedules playback via the
 * audio backend. Returns the number of sounds actually started.
 * This function is safe to call when audio is disabled (noop backend) -
 * it will still consume RNG rolls but not play audio. */
int esheep_sound_cache_trigger(EsheepSoundCache *cache,
                               int animation_id,
                               int frame_index);

/* Check if the cache has any sounds loaded. */
gboolean esheep_sound_cache_has_sounds(const EsheepSoundCache *cache);

/* Get total number of sound entries in the cache. */
int esheep_sound_cache_entry_count(const EsheepSoundCache *cache);

#endif /* ESHEEP_SOUND_CACHE_H */
