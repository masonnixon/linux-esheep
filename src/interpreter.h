#ifndef ESHEEP_INTERPRETER_H
#define ESHEEP_INTERPRETER_H

#include "animations_data.h"
#include <stdbool.h>

typedef struct {
    int animation_id;
    int frame_index;
    int elapsed_ms;
    int repeat_index;
    int area_width;
    int area_height;
    int image_width;
    int image_height;
} EsheepState;

void esheep_init(EsheepState *state, int animation_id);
void esheep_set_environment(EsheepState *state, int area_width, int area_height,
                            int image_width, int image_height);
int esheep_current_tile(const EsheepState *state);
/* Advance one animation frame when its interval has elapsed. Returns true
 * when a frame boundary was crossed, including repeat and wrap boundaries. */
bool esheep_tick(EsheepState *state, int dt_ms, const char *context, int roll_0_99);
bool esheep_border_event(EsheepState *state, const char *context, int roll_0_99);
bool esheep_gravity_event(EsheepState *state, const char *context, int roll_0_99);

#endif /* ESHEEP_INTERPRETER_H */
