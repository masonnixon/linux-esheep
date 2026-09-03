#ifndef ESHEEP_ACTOR_H
#define ESHEEP_ACTOR_H

#include "animations_data.h"
#include "interpreter.h"
#include <stdbool.h>

#define ESHEEP_ACTOR_MAX_CHILDREN 8
#define ESHEEP_ACTOR_MAX_DEPTH 4

typedef struct EsheepActor EsheepActor;

/* Injectable 0..99 random source. Values outside the range are clamped, so a
 * misbehaving source can still be used safely. */
typedef int (*EsheepActorRandom)(void *context);

/* Platform-independent animation actor. The engine owns the animation state,
 * position, direction, visibility, and tree links; it never renders and never
 * moves the actor. Frame stepping, repeat handling, and sequence/border/
 * gravity transition rules come from the interpreter and generated data, not
 * from this module. */
struct EsheepActor {
    EsheepState state;
    int x;
    int y;
    int direction; /* -1 = left, +1 = right */
    bool visible;
    EsheepActor *parent;
    EsheepActor *children[ESHEEP_ACTOR_MAX_CHILDREN];
    int child_count;
    int depth; /* 0 for roots, one more than the parent's depth */
    EsheepActorRandom random;
    void *random_context;
};

/* Initialize a fresh actor. `parent` may be NULL for a root; when non-NULL,
 * initialization attaches through the same capacity/cycle/depth checks as
 * esheep_actor_add_child. A rejected attachment leaves a valid detached
 * actor. Do not call this on an actor that currently has children or a
 * parent; use esheep_actor_add_child to (re)attach an existing actor. */
void esheep_actor_init(EsheepActor *actor, EsheepActor *parent,
                       int animation_id, int x, int y, int direction);

/* Replace the random source. Passing NULL restores the built-in
 * deterministic source. */
void esheep_actor_set_random_source(EsheepActor *actor, EsheepActorRandom random,
                                    void *context);
int esheep_actor_random(EsheepActor *actor);

/* Attach `child` under `parent`, resetting the child's owned animation
 * state for `animation_id` at (x, y). `child` storage must be a fresh
 * esheep_actor_init result or a previously attached/detached actor. A child
 * already attached elsewhere is moved with its subtree and random source
 * intact. Returns the child on success, NULL when the link would exceed
 * ESHEEP_ACTOR_MAX_DEPTH, overflow the child capacity, form a cycle, or the
 * arguments are invalid. Child-of-child trees are supported up to
 * ESHEEP_ACTOR_MAX_DEPTH below the root. */
EsheepActor *esheep_actor_add_child(EsheepActor *parent, EsheepActor *child,
                                    int animation_id, int x, int y,
                                    int direction);

/* Remove `actor` from its parent and clean up the whole subtree below it:
 * every descendant is recursively detached and left unlinked so the tree
 * stays well-formed. The actors' storage stays valid and reusable. */
void esheep_actor_detach(EsheepActor *actor);

/* Advance the actor and every descendant by dt_ms. Each actor draws its own
 * random source, so sibling transitions stay independent. Returns true when
 * any actor in the subtree crossed a frame boundary. */
bool esheep_actor_tick(EsheepActor *actor, int dt_ms, const char *context);

/* Dispatch the actor's authored border/gravity transitions for `context`
 * (NULL, "none", "window", "taskbar", "vertical", ...). Applies to the actor
 * only; child props are visual-only. */
bool esheep_actor_border_event(EsheepActor *actor, const char *context);
bool esheep_actor_gravity_event(EsheepActor *actor, const char *context);

/* Look up the authored child record (if any) for a parent animation id.
 * Finding a record does NOT create a child actor; the platform must call
 * esheep_actor_add_child explicitly. */
const EsheepChild *esheep_actor_child_record(int parent_animation_id);

/* Return every authored child record for a parent animation. The returned
 * count is bounded by `capacity`; records remain owned by the active data
 * table and must not be freed by the caller. */
int esheep_actor_child_records(int parent_animation_id,
                               const EsheepChild **records,
                               int capacity);

/* Count all descendants (children, grandchildren, ...). */
int esheep_actor_count_descendants(EsheepActor *actor);

#endif /* ESHEEP_ACTOR_H */
