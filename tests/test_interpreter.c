#include <assert.h>
#include <stdio.h>
#include "interpreter.h"
#include "animations_data.h"

int main() {
    EsheepState state;
    
    /* Test 1: Init on animation id 1 ("walk"), frames [2,3], interval 200ms.
       Drive 4 ticks with dt=200ms, context "none". Repeat=20, so no transition.
       Tile alternates: start=2, then 3, 2, 3, 2. */
    esheep_init(&state, 1);
    assert(state.animation_id == 1);
    assert(esheep_current_tile(&state) == 2);  /* frame 0 -> tile 2 */
    for (int i = 0; i < 4; i++) {
        bool t = esheep_tick(&state, 200, "none", 0);
        assert(t == true);
        int expected = (i % 2 == 0) ? 3 : 2;
        assert(esheep_current_tile(&state) == expected);
    }
    
    /* Test 2: Exhaust repeat count (20) then transition.
       anim1 sequence_next: context "none" eligible: {90,"none",1}, {6,"none",15}.
       After 20 repeats (=40 frame advances), the 40th tick triggers transition.
       With roll=0, accumulated=90 > 0, so picks anim 1. */
    esheep_init(&state, 1);
    bool last_t = false;
    for (int i = 0; i < 39; i++) {  /* 39 ticks of advance */
        last_t = esheep_tick(&state, 200, "none", 0);
    }
    assert(last_t == true);
    last_t = esheep_tick(&state, 200, "none", 0);  /* 40th tick = transition */
    assert(last_t == true);
    assert(state.animation_id == 1);
    
    /* Test 3: Different roll -> different eligible entry. roll=92 lands in anim 15.
       Eligible none: {90,"none",1} then {6,"none",15} total 96.
       roll=92 -> 92 < 90+6=96 but >=90, so anim 15. */
    esheep_init(&state, 1);
    for (int i = 0; i < 39; i++) {
        esheep_tick(&state, 200, "none", 0);
    }
    last_t = esheep_tick(&state, 200, "none", 92);
    assert(last_t == true);
    assert(state.animation_id == 15);
    
    /* Test 4: Context filtering. anim1 seq has window/taskbar/none entries.
       With context "none" and roll=1, should target anim 1 (roll<90), NOT anim 11 (window). */
    esheep_init(&state, 1);
    for (int i = 0; i < 39; i++) {
        esheep_tick(&state, 200, "none", 0);
    }
    last_t = esheep_tick(&state, 200, "none", 1);
    assert(last_t == true);
    assert(state.animation_id == 1);  /* NOT anim 11 */
    
    /* Test 5: Border event. anim1 border_next: {100,"none",2}, {2,"vertical",37}, {20,"window",43}.
       context="none", roll=50: accumulated=100 > 50, target=2. */
    esheep_init(&state, 1);
    bool border_changed = esheep_border_event(&state, "none", 50);
    assert(border_changed == true);
    assert(state.animation_id == 2);
    
    /* Test 6: Gravity event. anim1 gravity_next: {100,"none",5}.
       context="none", roll=50: accumulated=100 > 50, target=5. */
    esheep_init(&state, 1);
    bool gravity_changed = esheep_gravity_event(&state, "none", 50);
    assert(gravity_changed == true);
    assert(state.animation_id == 5);
    
    /* Test 7: Empty eligible subset. Use context that matches no border_next "only". */
    esheep_init(&state, 1);
    bool no_change = esheep_border_event(&state, "nonexistent_context", 50);
    assert(no_change == false);
    assert(state.animation_id == 1);
    
    /* Test 8: repeat_from regression. Animation 13 "kill" has frames [3,96,96],
       interval 100ms, repeat="20", repeat_from="1". After the sequence plays
       through once and wraps, frame_index must return to repeat_from (1),
       landing on tile 96 -- NOT back to frame 0 (tile 3). A prior version of
       esheep_tick always reset to frame_index 0 on wrap, ignoring
       repeat_from entirely; this is a real divergence in the source data
       (also affects animations 24, 26, 37), not a hypothetical case. */
    esheep_init(&state, 13);
    assert(esheep_current_tile(&state) == 3);   /* frame 0 */
    esheep_tick(&state, 100, "none", 0);
    assert(esheep_current_tile(&state) == 96);  /* frame 1 */
    esheep_tick(&state, 100, "none", 0);
    assert(esheep_current_tile(&state) == 96);  /* frame 2 */
    bool wrapped = esheep_tick(&state, 100, "none", 0);  /* frame_index wraps */
    assert(wrapped == true);  /* repeat_index 1 of 20, no transition yet */
    assert(esheep_current_tile(&state) == 96);  /* wraps to repeat_from=1, not 0 */

    /* Test 9: Size-dependent repeat expressions remain finite and do not
       collapse to zero through atoi. */
    esheep_init(&state, 21);
    esheep_set_environment(&state, 1920, 1080, 40, 40);
    esheep_tick(&state, 30, "none", 0);
    assert(state.animation_id == 21);
    assert(state.repeat_index == 1);

    /* Test 10: Interval overshoot is carried into the next frame. */
    esheep_init(&state, 1);
    esheep_tick(&state, 250, "none", 0);
    assert(state.frame_index == 1);
    assert(state.elapsed_ms == 50);

    /* Test 11: repeat=0 means play once, then follow sequence_next. */
    esheep_init(&state, 2);  /* rotate1a -> rotate1b */
    esheep_tick(&state, 200, "none", 0);
    esheep_tick(&state, 200, "none", 0);
    assert(state.animation_id == 2);
    esheep_tick(&state, 200, "none", 0);
    assert(state.animation_id == 3);

    /* Test 12: screen-width repeat expressions are evaluated against the
       configured monitor instead of falling back to one repetition. */
    esheep_set_environment(&state, 1920, 1080, 40, 40);
    esheep_init(&state, 28);
    esheep_tick(&state, 100, "none", 0);
    esheep_tick(&state, 100, "none", 0);
    esheep_tick(&state, 100, "none", 0);
    assert(state.repeat_index == 1);
    esheep_init(&state, 29);
    esheep_tick(&state, 100, "none", 0);
    esheep_tick(&state, 100, "none", 0);
    assert(state.repeat_index == 1);

    /* Test 13: sequence transitions retain timer overshoot for the target
       animation instead of introducing a silent timing pause. */
    esheep_init(&state, 2);
    esheep_tick(&state, 250, "none", 0);
    esheep_tick(&state, 200, "none", 0);
    esheep_tick(&state, 200, "none", 0);
    assert(state.animation_id == 3);
    assert(state.elapsed_ms == 50);

    printf("All tests passed\n");
    return 0;
}
