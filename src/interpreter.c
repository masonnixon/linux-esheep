#include "interpreter.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static const EsheepAnimation* get_animation(int id) {
    if (id < 1 || id > esheep_animation_count) return NULL;
    return &esheep_animations[id-1];
}

static int repeat_value(const char *repeat_str) {
    if (!repeat_str) return 0;
    return atoi(repeat_str);
}

void esheep_init(EsheepState *state, int animation_id) {
    state->animation_id = animation_id;
    state->frame_index = 0;
    state->elapsed_ms = 0;
    state->repeat_index = 0;
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

    state->elapsed_ms = 0;
    state->frame_index++;

    if (state->frame_index >= anim->frame_count) {
        state->frame_index = repeat_value(anim->repeat_from);
        int repeat_count = repeat_value(anim->repeat);
        if (repeat_count == 0) {
            return true;
        }
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
