#include "actor.h"
#include <stddef.h>

static unsigned int esheep_actor_seed = 0x9E3779B9u;

/* Built-in fallback: a deterministic 32-bit LCG so runs without an injected
 * source are still reproducible. */
static int esheep_actor_default_random(void *context) {
    (void)context;
    esheep_actor_seed = esheep_actor_seed * 1664525u + 1013904223u;
    return (int)((esheep_actor_seed >> 8) % 100u);
}

void esheep_actor_init(EsheepActor *actor, EsheepActor *parent,
                       int animation_id, int x, int y, int direction) {
    if (!actor) return;
    esheep_init(&actor->state, animation_id);
    actor->x = x;
    actor->y = y;
    actor->direction = direction < 0 ? -1 : 1;
    actor->visible = true;
    actor->parent = NULL;
    for (int i = 0; i < ESHEEP_ACTOR_MAX_CHILDREN; i++)
        actor->children[i] = NULL;
    actor->child_count = 0;
    actor->depth = 0;
    actor->random = NULL;
    actor->random_context = NULL;
    if (parent) {
        /* Use the normal validated link path so the parent's child list,
         * depth, and capacity remain consistent. A rejected link leaves a
         * fully initialized detached actor. */
        (void)esheep_actor_add_child(parent, actor, animation_id, x, y,
                                     direction);
    }
}

void esheep_actor_set_random_source(EsheepActor *actor, EsheepActorRandom random,
                                    void *context) {
    if (!actor) return;
    actor->random = random;
    actor->random_context = context;
}

int esheep_actor_random(EsheepActor *actor) {
    int value = actor->random ? actor->random(actor->random_context)
                              : esheep_actor_default_random(NULL);
    if (value < 0) return 0;
    if (value > 99) return 99;
    return value;
}

static void unlink_child(EsheepActor *parent, EsheepActor *child) {
    for (int i = 0; i < parent->child_count; i++) {
        if (parent->children[i] != child) continue;
        for (int j = i; j + 1 < parent->child_count; j++)
            parent->children[j] = parent->children[j + 1];
        parent->children[parent->child_count - 1] = NULL;
        parent->child_count--;
        return;
    }
}

static int subtree_height(const EsheepActor *actor) {
    int height = 1;
    for (int i = 0; i < actor->child_count; i++) {
        int child_height = 1 + subtree_height(actor->children[i]);
        if (child_height > height) height = child_height;
    }
    return height;
}

static void set_subtree_depth(EsheepActor *actor, int depth) {
    actor->depth = depth;
    for (int i = 0; i < actor->child_count; i++)
        set_subtree_depth(actor->children[i], depth + 1);
}

EsheepActor *esheep_actor_add_child(EsheepActor *parent, EsheepActor *child,
                                    int animation_id, int x, int y,
                                    int direction) {
    if (!parent || !child || child == parent) return NULL;
    for (EsheepActor *ancestor = parent; ancestor; ancestor = ancestor->parent)
        if (ancestor == child) return NULL; /* would form a cycle */
    if (parent->depth + subtree_height(child) > ESHEEP_ACTOR_MAX_DEPTH)
        return NULL;
    if (parent->child_count >= ESHEEP_ACTOR_MAX_CHILDREN) return NULL;

    if (child->parent) unlink_child(child->parent, child);
    else {
        for (int i = 0; i < ESHEEP_ACTOR_MAX_CHILDREN; i++)
            child->children[i] = NULL;
        child->child_count = 0;
        child->random = NULL;
        child->random_context = NULL;
    }

    esheep_init(&child->state, animation_id);
    child->x = x;
    child->y = y;
    child->direction = direction < 0 ? -1 : 1;
    child->visible = true;
    child->parent = parent;
    parent->children[parent->child_count++] = child;
    set_subtree_depth(child, parent->depth + 1);
    return child;
}

void esheep_actor_detach(EsheepActor *actor) {
    if (!actor) return;
    for (int i = actor->child_count - 1; i >= 0; i--)
        esheep_actor_detach(actor->children[i]);
    for (int i = 0; i < actor->child_count; i++)
        actor->children[i] = NULL;
    actor->child_count = 0;
    if (actor->parent) unlink_child(actor->parent, actor);
    actor->parent = NULL;
    actor->depth = 0;
}

bool esheep_actor_tick(EsheepActor *actor, int dt_ms, const char *context) {
    if (!actor) return false;
    bool stepped = esheep_tick(&actor->state, dt_ms, context,
                               esheep_actor_random(actor));
    for (int i = 0; i < actor->child_count; i++)
        stepped = esheep_actor_tick(actor->children[i], dt_ms, context) ||
                  stepped;
    return stepped;
}

bool esheep_actor_border_event(EsheepActor *actor, const char *context) {
    if (!actor) return false;
    return esheep_border_event(&actor->state, context,
                               esheep_actor_random(actor));
}

bool esheep_actor_gravity_event(EsheepActor *actor, const char *context) {
    if (!actor) return false;
    return esheep_gravity_event(&actor->state, context,
                                esheep_actor_random(actor));
}

const EsheepChild *esheep_actor_child_record(int parent_animation_id) {
    for (int i = 0; i < esheep_child_count; i++) {
        if (esheep_childs[i].animation_id == parent_animation_id)
            return &esheep_childs[i];
    }
    return NULL;
}

int esheep_actor_child_records(int parent_animation_id,
                               const EsheepChild **records,
                               int capacity) {
    if (capacity < 0) capacity = 0;
    int total = 0;
    for (int i = 0; i < esheep_child_count; i++) {
        if (esheep_childs[i].animation_id != parent_animation_id) continue;
        if (records && total < capacity) records[total] = &esheep_childs[i];
        total++;
    }
    return total;
}

int esheep_actor_count_descendants(EsheepActor *actor) {
    if (!actor) return 0;
    int count = 0;
    for (int i = 0; i < actor->child_count; i++)
        count += 1 + esheep_actor_count_descendants(actor->children[i]);
    return count;
}
