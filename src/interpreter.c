#include "interpreter.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

static const EsheepAnimation* get_animation(int id) {
    if (id < 1 || id > esheep_animation_count) return NULL;
    return &esheep_animations[id-1];
}

static int default_area_width = 1920;
static int default_area_height = 1080;
static int default_image_width = 40;
static int default_image_height = 40;

static int repeat_value(const EsheepState *state, const char *repeat_str,
                        int roll_0_99) {
    if (!repeat_str) return 0;
    char *end = NULL;
    long value = strtol(repeat_str, &end, 10);
    if (end != repeat_str && *end == '\0') return (int)value;

    /* The source data uses a small expression vocabulary for random repeat
     * counts.  Keep the evaluator deterministic by deriving random from the
     * tick's supplied 0..99 roll. */
    int divisor = 0;
    int addend = 0;
    if (sscanf(repeat_str, "random/%d+%d", &divisor, &addend) == 2 &&
        divisor > 0)
        return roll_0_99 / divisor + addend;
    if (sscanf(repeat_str, "%d+random/%d", &addend, &divisor) == 2 &&
        divisor > 0)
        return addend + roll_0_99 / divisor;
    int offset;
    if (sscanf(repeat_str,
               "(areaH/2+(randS*areaH/2)/120-imageH-%d)/2", &offset) == 1 &&
        state->area_height > 0)
        return (state->area_height / 2 +
                (roll_0_99 * state->area_height / 2) / 120 -
                state->image_height - offset) / 2;
    return 1;
}

void esheep_init(EsheepState *state, int animation_id) {
    state->animation_id = animation_id;
    state->frame_index = 0;
    state->elapsed_ms = 0;
    state->repeat_index = 0;
    state->area_width = default_area_width;
    state->area_height = default_area_height;
    state->image_width = default_image_width;
    state->image_height = default_image_height;
}

void esheep_set_environment(EsheepState *state, int area_width, int area_height,
                            int image_width, int image_height) {
    state->area_width = area_width;
    state->area_height = area_height;
    state->image_width = image_width;
    state->image_height = image_height;
    default_area_width = area_width;
    default_area_height = area_height;
    default_image_width = image_width;
    default_image_height = image_height;
}

int esheep_current_tile(const EsheepState *state) {
    const EsheepAnimation *anim = get_animation(state->animation_id);
    if (!anim) return 0;
    if (state->frame_index < 0 || state->frame_index >= anim->frame_count) return 0;
    return anim->frames[state->frame_index];
}

static bool check_only(const char *only, const char *context) {
    if (!only) return true;
    if (!context) return false;
    return strcmp(only, context) == 0;
}

static int roll_for_transition(const EsheepTransition *transitions, int count, 
                              const char *context, int roll_0_99) {
    int accumulated = 0;
    for (int i = 0; i < count; i++) {
        if (!check_only(transitions[i].only, context)) continue;
        accumulated += transitions[i].probability;
        if (roll_0_99 < accumulated) {
            return transitions[i].target;
        }
    }
    return -1;
}

bool esheep_tick(EsheepState *state, int dt_ms, const char *context, int roll_0_99) {
    const EsheepAnimation *anim = get_animation(state->animation_id);
    if (!anim) return false;

    state->elapsed_ms += dt_ms;
    if (state->elapsed_ms < anim->start.interval_ms) {
        return false;
    }

    state->elapsed_ms -= anim->start.interval_ms;
    state->frame_index++;

    if (state->frame_index >= anim->frame_count) {
        state->frame_index = repeat_value(state, anim->repeat_from, roll_0_99);
        int repeat_count = repeat_value(state, anim->repeat, roll_0_99);
        state->repeat_index++;
        if (state->repeat_index >= repeat_count) {
            int target = roll_for_transition(anim->sequence_next, anim->sequence_next_count,
                                           context, roll_0_99);
            if (target >= 0) {
                state->animation_id = target;
                state->frame_index = 0;
                state->elapsed_ms = 0;
                state->repeat_index = 0;
                return true;
            }
            return true;
        }
        return true;
    }
    return true;
}

bool esheep_border_event(EsheepState *state, const char *context, int roll_0_99) {
    const EsheepAnimation *anim = get_animation(state->animation_id);
    if (!anim) return false;

    int target = roll_for_transition(anim->border_next, anim->border_next_count,
                                   context, roll_0_99);
    if (target >= 0) {
        state->animation_id = target;
        state->frame_index = 0;
        state->elapsed_ms = 0;
        state->repeat_index = 0;
        return true;
    }
    return false;
}

bool esheep_gravity_event(EsheepState *state, const char *context, int roll_0_99) {
    const EsheepAnimation *anim = get_animation(state->animation_id);
    if (!anim) return false;

    int target = roll_for_transition(anim->gravity_next, anim->gravity_next_count,
                                   context, roll_0_99);
    if (target >= 0) {
        state->animation_id = target;
        state->frame_index = 0;
        state->elapsed_ms = 0;
        state->repeat_index = 0;
        return true;
    }
    return false;
}
