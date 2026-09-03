#include <assert.h>
#include <stdio.h>
#include "actor.h"
#include "animations_data.h"
#include "interpreter.h"

static int fixed_roll(void *context) { return *(int *)context; }
static int over_roll(void *context) { (void)context; return 250; }
static int under_roll(void *context) { (void)context; return -7; }
static int helper_rolls[128];
static int helper_roll_count;

static int *next_helper_roll(int value) {
    assert(helper_roll_count < (int)(sizeof(helper_rolls) /
                                     sizeof(helper_rolls[0])));
    helper_rolls[helper_roll_count] = value;
    return &helper_rolls[helper_roll_count++];
}

static EsheepActor *add_at(EsheepActor *parent, EsheepActor *child,
                           int animation_id, int x, int y, int roll) {
    esheep_actor_init(child, NULL, animation_id, x, y, 1);
    EsheepActor *linked = esheep_actor_add_child(parent, child, animation_id,
                                                 x, y, 1);
    if (linked) esheep_actor_set_random_source(linked, fixed_roll,
                                                next_helper_roll(roll));
    return linked;
}

static EsheepActor *root_actor(EsheepActor *actor, int animation_id,
                               int x, int y, int direction, int roll) {
    esheep_actor_init(actor, NULL, animation_id, x, y, direction);
    esheep_actor_set_random_source(actor, fixed_roll, next_helper_roll(roll));
    return actor;
}

/* Independent frame timing: same dt drives both actors, but each follows its
 * own authored intervals, so advance counts and timers diverge. */
static void test_independent_frame_timing(void) {
    EsheepActor parent, child;
    root_actor(&parent, 1, 100, 200, -1, 0);      /* walk: 200ms frames */
    add_at(&parent, &child, 14, 110, 210, 50);    /* sync: 30ms -> 100ms */

    for (int i = 0; i < 3; i++)
        assert(esheep_actor_tick(&parent, 100, "none"));

    assert(parent.state.frame_index == 1);
    assert(parent.state.repeat_index == 0);
    assert(parent.state.elapsed_ms == 100);
    assert(child.state.frame_index == 1);
    assert(child.state.repeat_index == 2);
    assert(child.state.elapsed_ms == 10);
    assert(child.state.animation_id == 14);
    assert(child.parent == &parent);
}

/* Deterministic transition selection through injected random sources. */
static void test_deterministic_transitions(void) {
    EsheepActor actor;
    int roll = 92;
    esheep_actor_init(&actor, NULL, 1, 0, 0, 1);
    esheep_actor_set_random_source(&actor, fixed_roll, &roll);
    for (int i = 0; i < 40; i++)
        esheep_actor_tick(&actor, 200, "none");
    assert(actor.state.animation_id == 15);   /* roll 92 -> sleep route */
    assert(actor.state.frame_index == 0);
    assert(actor.state.repeat_index == 0);

    roll = 50;
    esheep_actor_init(&actor, NULL, 1, 0, 0, 1);
    esheep_actor_set_random_source(&actor, fixed_roll, &roll);
    for (int i = 0; i < 40; i++)
        esheep_actor_tick(&actor, 200, "none");
    assert(actor.state.animation_id == 1);    /* roll 50 -> keep walking */

    /* Same setup, same source: identical outcome (deterministic). */
    roll = 92;
    esheep_actor_init(&actor, NULL, 1, 0, 0, 1);
    esheep_actor_set_random_source(&actor, fixed_roll, &roll);
    for (int i = 0; i < 40; i++)
        esheep_actor_tick(&actor, 200, "none");
    assert(actor.state.animation_id == 15);

    /* Out-of-range source values are clamped into 0..99. */
    esheep_actor_init(&actor, NULL, 1, 0, 0, 1);
    esheep_actor_set_random_source(&actor, over_roll, NULL);
    assert(esheep_actor_random(&actor) == 99);
    esheep_actor_set_random_source(&actor, under_roll, NULL);
    assert(esheep_actor_random(&actor) == 0);
}

/* A child's transition roll is its own, not the parent's. */
static void test_child_uses_own_roll(void) {
    EsheepActor parent, child;
    int parent_roll = 92, child_roll = 0;
    esheep_actor_init(&parent, NULL, 1, 0, 0, 1);
    esheep_actor_set_random_source(&parent, fixed_roll, &parent_roll);
    esheep_actor_add_child(&parent, &child, 3, 10, 20, 1);
    esheep_actor_set_random_source(&child, fixed_roll, &child_roll);

    for (int i = 0; i < 3; i++)
        esheep_actor_tick(&parent, 200, "none");

    /* rotate1b (anim 3) plays 3 frames at 200ms with repeat 0, so it
     * transitions on its third tick with roll 0 -> anim 1, while the parent
     * (roll 92) is still mid-walk. */
    assert(child.state.animation_id == 1);
    assert(parent.state.animation_id == 1);
    assert(parent.state.repeat_index == 1);
    assert(parent.state.frame_index == 1);
}

/* Multiple children per parent, each on its own animation and clock. */
static void test_multiple_children(void) {
    EsheepActor parent, c1, c2, c3;
    root_actor(&parent, 1, 0, 0, 1, 0);
    assert(add_at(&parent, &c1, 2, 0, 0, 0));    /* rotate1a: 200ms */
    assert(add_at(&parent, &c2, 21, 0, 0, 0));   /* batha: 30ms, 1 frame */
    assert(add_at(&parent, &c3, 27, 0, 0, 0));   /* flower: 300ms */
    assert(parent.child_count == 3);
    assert(parent.children[0] == &c1);
    assert(parent.children[1] == &c2);
    assert(parent.children[2] == &c3);

    for (int i = 0; i < 3; i++)
        esheep_actor_tick(&parent, 100, "none");

    assert(c1.state.frame_index == 1);           /* 2 of 3 ticks crossed 200ms */
    assert(c1.state.animation_id == 2);
    assert(c2.state.animation_id == 21);
    assert(c2.state.repeat_index == 10);        /* 30ms frames with carry */
    assert(c3.state.frame_index == 1);           /* 300ms crossed once */
    assert(parent.state.animation_id == 1);
}

/* Child-of-child fixture: a three-level tree ticks as one unit. */
static void test_child_of_child(void) {
    EsheepActor root, mid, leaf;
    root_actor(&root, 1, 100, 200, -1, 0);
    assert(add_at(&root, &mid, 2, 140, 220, 0));
    assert(add_at(&mid, &leaf, 27, 180, 240, 0));

    assert(root.depth == 0);
    assert(mid.depth == 1);
    assert(leaf.depth == 2);
    assert(mid.parent == &root);
    assert(leaf.parent == &mid);
    assert(root.parent == NULL);
    assert(esheep_actor_count_descendants(&root) == 2);

    assert(!esheep_actor_tick(&root, 100, "none")); /* nobody crossed a boundary yet */
    assert(esheep_actor_tick(&root, 100, "none"));
    assert(esheep_actor_tick(&root, 100, "none"));

    assert(root.state.frame_index == 1);
    assert(mid.state.frame_index == 1);
    assert(leaf.state.frame_index == 1);
    assert(root.state.elapsed_ms == 100);
    assert(mid.state.elapsed_ms == 100);
    assert(leaf.state.elapsed_ms == 0);
    assert(root.x == 100 && root.y == 200);
    assert(mid.x == 140 && mid.y == 220);
    assert(leaf.x == 180 && leaf.y == 240);
    assert(root.direction == -1);
    assert(mid.direction == 1);
    assert(leaf.state.animation_id == 27);
}

/* Detaching a parent cleans up every descendant, and actors stay reusable. */
static void test_parent_removal_cleanup(void) {
    EsheepActor root, c1, c2, c3;
    root_actor(&root, 1, 0, 0, 1, 0);
    add_at(&root, &c1, 2, 0, 0, 0);
    add_at(&c1, &c2, 27, 0, 0, 0);
    add_at(&root, &c3, 21, 0, 0, 0);
    assert(esheep_actor_count_descendants(&root) == 3);

    esheep_actor_detach(&root);

    assert(root.child_count == 0);
    assert(root.depth == 0);
    assert(c1.parent == NULL && c1.child_count == 0 && c1.depth == 0);
    assert(c2.parent == NULL && c2.child_count == 0 && c2.depth == 0);
    assert(c3.parent == NULL && c3.child_count == 0 && c3.depth == 0);
    assert(esheep_actor_count_descendants(&root) == 0);

    /* Reattach a cleaned-up descendant; its state is fresh for the new use. */
    assert(esheep_actor_add_child(&root, &c3, 21, 5, 6, 1));
    assert(root.child_count == 1);
    assert(c3.parent == &root);
    assert(c3.depth == 1);
    assert(c3.state.animation_id == 21);
    assert(c3.x == 5 && c3.y == 6);
}

/* Detaching a middle actor removes it from its parent and cleans its subtree
 * without touching the rest of the tree. */
static void test_middle_detach(void) {
    EsheepActor root, middle, leaf, sibling;
    root_actor(&root, 1, 0, 0, 1, 0);
    add_at(&root, &middle, 2, 0, 0, 0);
    add_at(&middle, &leaf, 27, 0, 0, 0);
    add_at(&root, &sibling, 14, 0, 0, 0);
    assert(root.child_count == 2);

    esheep_actor_detach(&middle);

    assert(middle.parent == NULL);
    assert(middle.child_count == 0);
    assert(leaf.parent == NULL);
    assert(root.child_count == 1);
    assert(root.children[0] == &sibling);
    assert(sibling.parent == &root);
    assert(leaf.state.animation_id == 27); /* state preserved, links cleared */
}

/* Maximum depth and cycle protection. */
static void test_depth_and_cycle_rejection(void) {
    EsheepActor root, d1, d2, d3, d4, d5;
    root_actor(&root, 1, 0, 0, 1, 0);
    assert(add_at(&root, &d1, 2, 0, 0, 0));
    assert(add_at(&d1, &d2, 3, 0, 0, 0));
    assert(add_at(&d2, &d3, 14, 0, 0, 0));
    assert(add_at(&d3, &d4, 15, 0, 0, 0));
    assert(d4.depth == ESHEEP_ACTOR_MAX_DEPTH);

    assert(add_at(&d4, &d5, 2, 0, 0, 0) == NULL);  /* depth 5 exceeds limit */
    assert(esheep_actor_add_child(&d3, &root, 2, 0, 0, 1) == NULL); /* cycle */
    assert(esheep_actor_add_child(&d1, &d1, 2, 0, 0, 1) == NULL);   /* self */
    assert(d3.child_count == 1);   /* rejected adds left the tree intact */
    assert(d3.children[0] == &d4);
    assert(d4.parent == &d3);
    assert(root.child_count == 1);

    /* Moving a whole subtree is allowed when the link stays acyclic and
     * within the depth budget; depths are recomputed. */
    assert(esheep_actor_add_child(&root, &d3, 31, 7, 8, 1));
    assert(d3.parent == &root);
    assert(d3.depth == 1);
    assert(d4.depth == 2);
    assert(d2.child_count == 0);
    assert(root.child_count == 2);

    /* The moved subtree can still slide down until the chain hits the limit. */
    assert(esheep_actor_add_child(&d2, &d3, 31, 0, 0, 1));
    assert(d3.depth == 3);
    assert(d4.depth == 4);
    assert(root.child_count == 1);
    assert(d4.parent == &d3);

    /* A full-depth chain cannot be extended (and re-linking an ancestor is
     * a cycle either way). */
    assert(esheep_actor_add_child(&d4, &d1, 2, 0, 0, 1) == NULL);
    assert(d1.parent == &root);
}

/* Child capacity per parent. */
static void test_child_capacity(void) {
    EsheepActor root;
    EsheepActor kids[ESHEEP_ACTOR_MAX_CHILDREN + 1];
    root_actor(&root, 1, 0, 0, 1, 0);
    for (int i = 0; i < ESHEEP_ACTOR_MAX_CHILDREN; i++)
        assert(add_at(&root, &kids[i], 2, i, 0, 0) != NULL);
    assert(root.child_count == ESHEEP_ACTOR_MAX_CHILDREN);
    assert(add_at(&root, &kids[ESHEEP_ACTOR_MAX_CHILDREN], 2, 0, 0, 0) == NULL);
    assert(root.child_count == ESHEEP_ACTOR_MAX_CHILDREN);
}

/* A child record that merely exists in the generated data must not create a
 * child actor: only an explicit add_child spawns one. */
static void test_child_record_alone_spawns_nothing(void) {
    const EsheepChild *record = esheep_actor_child_record(26);
    assert(record != NULL);
    assert(record->next == 27);
    assert(esheep_actor_child_record(21) &&
           esheep_actor_child_record(21)->next == 23);
    assert(esheep_actor_child_record(28) &&
           esheep_actor_child_record(28)->next == 31);
    assert(esheep_actor_child_record(1) == NULL);

    const EsheepChild *records[ESHEEP_ACTOR_MAX_CHILDREN] = {0};
    assert(esheep_actor_child_records(26, records,
                                      ESHEEP_ACTOR_MAX_CHILDREN) == 1);
    assert(records[0] == record);
    assert(esheep_actor_child_records(1, records,
                                      ESHEEP_ACTOR_MAX_CHILDREN) == 0);

    EsheepActor parent, child;
    int roll = 0;
    esheep_actor_init(&parent, NULL, 26, 0, 0, 1);   /* eat: authored child 27 */
    esheep_actor_set_random_source(&parent, fixed_roll, &roll);

    for (int i = 0; i < 20; i++)
        esheep_actor_tick(&parent, 300, "none");
    assert(parent.state.animation_id == 26);
    assert(parent.child_count == 0);   /* no autospawn mid-sequence */

    for (int i = 0; i < 20; i++)
        esheep_actor_tick(&parent, 300, "none");
    assert(parent.state.animation_id == 1); /* full eat sequence completed */
    assert(parent.child_count == 0);        /* still none without an explicit add */

    esheep_actor_init(&child, NULL, record->next, 0, 0, 1);
    assert(esheep_actor_add_child(&parent, &child, record->next, 9, 9, 1));
    assert(parent.child_count == 1);
    assert(child.state.animation_id == 27);
    esheep_actor_tick(&parent, 300, "none");
    assert(child.state.frame_index == 1);
}

static void test_multiple_authored_child_records(void) {
    const EsheepChild authored[] = {
        { 26, "x", "y", 27 },
        { 26, "x+1", "y+1", 31 },
    };
    const EsheepChild *records[ESHEEP_ACTOR_MAX_CHILDREN] = {0};
    const EsheepChild *old_records = esheep_childs;
    int old_count = esheep_child_count;
    esheep_childs = authored;
    esheep_child_count = 2;

    assert(esheep_actor_child_records(26, records,
                                      ESHEEP_ACTOR_MAX_CHILDREN) == 2);
    assert(records[0] == &authored[0]);
    assert(records[1] == &authored[1]);

    /* The API reports the full count even when the output buffer is smaller. */
    assert(esheep_actor_child_records(26, records, 1) == 2);
    assert(esheep_actor_child_records(1, records,
                                      ESHEEP_ACTOR_MAX_CHILDREN) == 0);
    esheep_childs = old_records;
    esheep_child_count = old_count;
}

/* Owned props: direction normalization, visibility, and the engine never
 * moves the actor itself. */
static void test_owned_props(void) {
    EsheepActor actor;
    int roll = 0;
    esheep_actor_init(&actor, NULL, 1, 300, 400, -1);
    esheep_actor_set_random_source(&actor, fixed_roll, &roll);
    assert(actor.direction == -1);
    assert(actor.visible);

    esheep_actor_init(&actor, NULL, 1, 300, 400, 7);
    esheep_actor_set_random_source(&actor, fixed_roll, &roll);
    assert(actor.direction == 1);

    actor.visible = false;
    assert(esheep_actor_tick(&actor, 200, "none"));
    assert(!actor.visible);
    assert(actor.state.frame_index == 1);
    assert(actor.x == 300 && actor.y == 400);
}

/* Border and gravity events dispatch through the actor's own roll source. */
static void test_event_dispatch(void) {
    EsheepActor actor;
    int roll = 50;
    esheep_actor_init(&actor, NULL, 1, 0, 0, 1);
    esheep_actor_set_random_source(&actor, fixed_roll, &roll);

    assert(esheep_actor_border_event(&actor, "none"));
    assert(actor.state.animation_id == 2);

    esheep_actor_init(&actor, NULL, 1, 0, 0, 1);
    esheep_actor_set_random_source(&actor, fixed_roll, &roll);
    assert(esheep_actor_gravity_event(&actor, "none"));
    assert(actor.state.animation_id == 5);

    esheep_actor_init(&actor, NULL, 1, 0, 0, 1);
    esheep_actor_set_random_source(&actor, fixed_roll, &roll);
    assert(!esheep_actor_border_event(&actor, "no_such_context"));
    assert(actor.state.animation_id == 1);
}

int main(void) {
    test_independent_frame_timing();
    test_deterministic_transitions();
    test_child_uses_own_roll();
    test_multiple_children();
    test_child_of_child();
    test_parent_removal_cleanup();
    test_middle_detach();
    test_depth_and_cycle_rejection();
    test_child_capacity();
    test_child_record_alone_spawns_nothing();
    test_multiple_authored_child_records();
    test_owned_props();
    test_event_dispatch();

    printf("All actor tests passed\n");
    return 0;
}
