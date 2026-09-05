#include "interpreter.h"
#include <errno.h>
#include <limits.h>
#include <stdint.h>
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
static int default_walk_keep_probability = -1;

static int repeat_value(const EsheepState *state, const char *repeat_str,
                        int roll_0_99) {
    if (!repeat_str) return 0;
    char *end = NULL;
    errno = 0;
    long value = strtol(repeat_str, &end, 10);
    if (end != repeat_str && *end == '\0' && errno == 0 &&
        value >= INT_MIN && value <= INT_MAX)
        return (int)value;

    /* These are the repeat expression forms used by the authored package.
     * Parse into long values and narrow only after checking the result. */
    long divisor = 0;
    long addend = 0;
    if (sscanf(repeat_str, "random/%ld+%ld", &divisor, &addend) == 2 &&
        divisor > 0) {
        int64_t result = (int64_t)roll_0_99 / divisor + addend;
        if (result >= INT_MIN && result <= INT_MAX) return (int)result;
    }
    if (sscanf(repeat_str, "%ld+random/%ld", &addend, &divisor) == 2 &&
        divisor > 0) {
        int64_t result = addend + (int64_t)roll_0_99 / divisor;
        if (result >= INT_MIN && result <= INT_MAX) return (int)result;
    }
    long offset = 0;
    if (sscanf(repeat_str,
               "(areaH/2+(randS*areaH/2)/120-imageH-%ld)/2", &offset) == 1 &&
        state->area_height > 0) {
        int64_t result = ((int64_t)state->area_height / 2 +
                          ((int64_t)roll_0_99 * state->area_height / 2) / 120 -
                          state->image_height - offset) / 2;
        if (result >= INT_MIN && result <= INT_MAX) return (int)result;
    }
    if (strcmp(repeat_str, "(screenW/2)/30-6") == 0 && state->area_width > 0) {
        int64_t result = ((int64_t)state->area_width / 2) / 30 - 6;
        if (result >= INT_MIN && result <= INT_MAX) return (int)result;
    }
    if (strcmp(repeat_str,
               "24+(Convert(screenW/2,System.Int32)%30)/7") == 0 &&
        state->area_width > 0)
        return 24 + ((state->area_width / 2) % 30) / 7;
    if (strcmp(repeat_str,
               "25+(Convert(screenW/2,System.Int32)%30)/7") == 0 &&
        state->area_width > 0)
        return 25 + ((state->area_width / 2) % 30) / 7;
    return 1;
}

static int frame_interval(const EsheepAnimation *anim, int frame_index) {
    if (anim->frame_count <= 1) return anim->start.interval_ms;
    double progress = (double)frame_index / (double)(anim->frame_count - 1);
    double interval = anim->start.interval_ms +
                      (anim->end.interval_ms - anim->start.interval_ms) *
                      progress;
    return (int)(interval + 0.5);
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
    state->walk_keep_probability = default_walk_keep_probability;
    state->event_count = 0;
}

void esheep_set_walk_keep_probability(EsheepState *state, int probability) {
    if (probability < 0 || probability > 100) return;
    default_walk_keep_probability = probability;
    state->walk_keep_probability = probability;
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
    state->event_count = 0;
    state->elapsed_ms += dt_ms;

    while (state->event_count < ESHEEP_MAX_TICK_EVENTS) {
        const EsheepAnimation *anim = get_animation(state->animation_id);
        int interval = anim ? frame_interval(anim, state->frame_index) : 0;
        if (!anim || interval <= 0 || state->elapsed_ms < interval)
            break;

        state->elapsed_ms -= interval;
        state->events[state->event_count++] =
            (EsheepFrameEvent){ state->animation_id, state->frame_index };
        state->frame_index++;

        if (state->frame_index >= anim->frame_count) {
            state->frame_index = repeat_value(state, anim->repeat_from, roll_0_99);
            int repeat_count = repeat_value(state, anim->repeat, roll_0_99);
            state->repeat_index++;
            if (state->repeat_index >= repeat_count) {
                int transition_roll = roll_0_99;
                if (anim->id == 1 && context && strcmp(context, "none") == 0 &&
                    state->walk_keep_probability >= 0) {
                    int keep = state->walk_keep_probability;
                    if (roll_0_99 < keep)
                        transition_roll = 0;
                    else if (keep < 100)
                        transition_roll = 90 +
                            ((roll_0_99 - keep) * 6) / (100 - keep);
                }
                int target = roll_for_transition(anim->sequence_next,
                                                 anim->sequence_next_count,
                                                 context, transition_roll);
                if (target >= 0) {
                    state->animation_id = target;
                    state->frame_index = 0;
                    state->repeat_index = 0;
                }
            }
        }
    }
    return state->event_count > 0;
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
