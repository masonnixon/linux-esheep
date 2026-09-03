#include "pet_package.h"
#include "animations_data.h"
#include "renderer.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ESHEEP_PACKAGE_MAX_CHILD_DEPTH 4

typedef struct {
    EsheepTransition value;
} TransitionBuild;

typedef struct {
    EsheepAnimation value;
    gboolean has_start;
    gboolean has_end;
    gboolean has_sequence;
    GArray *frames;
    GArray *sequence_next;
    GArray *border_next;
    GArray *gravity_next;
} AnimationBuild;

typedef struct {
    EsheepSpawn value;
    GArray *next;
} SpawnBuild;

typedef enum {
    FIELD_NONE,
    FIELD_HEADER_TILES_X,
    FIELD_HEADER_TILES_Y,
    FIELD_ANIMATION_NAME,
    FIELD_POSE_X,
    FIELD_POSE_Y,
    FIELD_POSE_INTERVAL,
    FIELD_POSE_OFFSET_Y,
    FIELD_POSE_OPACITY,
    FIELD_REPEAT,
    FIELD_REPEAT_FROM,
    FIELD_ACTION,
    FIELD_FRAME,
    FIELD_NEXT,
    FIELD_SPAWN_X,
    FIELD_SPAWN_Y,
    FIELD_CHILD_X,
    FIELD_CHILD_Y,
    FIELD_CHILD_NEXT,
    FIELD_IMAGE_FILE
} Field;

typedef struct {
    EsheepTransition transition;
    gboolean has_probability;
    char *only;
    GString *text;
    GArray *destination;
} PendingNext;

struct EsheepPetPackage {
    int tiles_x;
    int tiles_y;
    EsheepSpawn *spawns;
    int spawn_count;
    EsheepAnimation *animations;
    int animation_count;
    EsheepChild *childs;
    int child_count;
    const char *spritesheet;
    GPtrArray *strings;
    GPtrArray *animation_builds;
    GPtrArray *spawn_builds;
    GArray *child_builds;
};

typedef struct {
    EsheepPetPackage *package;
    AnimationBuild *animation;
    SpawnBuild *spawn;
    EsheepChild child;
    gboolean in_child;
    gboolean in_start_pose;
    gboolean in_end_pose;
    Field field;
    PendingNext *pending_next;
    GString *text;
    GError **error;
    GArray *transition_destination;
} ParseState;

static void set_error(ParseState *state, const char *message) {
    if (state->error && !*state->error)
        g_set_error(state->error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                    "%s", message);
}

static char *owned_string(EsheepPetPackage *package, const char *value) {
    char *copy = g_strdup(value ? value : "");
    g_ptr_array_add(package->strings, copy);
    return copy;
}

static gboolean parse_int(const char *text, int *out) {
    char *end = NULL;
    long value;
    if (!text || !*text) return FALSE;
    errno = 0;
    value = strtol(text, &end, 10);
    if (errno || end == text || *end != '\0' || value < -2147483647L ||
        value > 2147483647L) return FALSE;
    if (out) *out = (int)value;
    return TRUE;
}

static gboolean parse_double(const char *text, double *out) {
    char *end = NULL;
    double value;
    if (!text || !*text) return FALSE;
    errno = 0;
    value = g_ascii_strtod(text, &end);
    if (errno || end == text || *end != '\0') return FALSE;
    if (out) *out = value;
    return TRUE;
}

static gboolean valid_integer_or_image_factor(const char *text) {
    int value;
    if (parse_int(text, &value)) return TRUE;
    if (g_str_has_prefix(text, "imageW*") || g_str_has_prefix(text, "imageH*")) {
        double factor;
        return parse_double(text + 7, &factor);
    }
    if (g_str_has_prefix(text, "-imageW*") || g_str_has_prefix(text, "-imageH*")) {
        double factor;
        return parse_double(text + 8, &factor);
    }
    return FALSE;
}

static gboolean valid_spawn_expression(const char *text) {
    static const char *const exact[] = {
        "screenW", "screenW+10", "areaH-imageH", "areaH/2-imageH",
        "areaH/2", "-imageH-20",
        "areaH/2-(randS*areaH/2)/120-imageH", NULL
    };
    int value;
    if (parse_int(text, &value)) return TRUE;
    for (int i = 0; exact[i]; i++)
        if (strcmp(text, exact[i]) == 0) return TRUE;
    return strstr(text, "random*(screenW-imageW-50)/100+25") != NULL;
}

static gboolean valid_repeat_expression(const char *text) {
    int value;
    if (parse_int(text, &value)) return TRUE;
    return strstr(text, "random/") != NULL || strstr(text, "+random/") != NULL ||
           strstr(text, "screenW/2") != NULL || strstr(text, "areaH/2") != NULL;
}

static gboolean valid_child_expression(const char *text) {
    static const char *const exact[] = {
        "imageX", "imageY", "-imageW", "-imageW-8",
        "imageX-imageW*0.9", "areaH-imageH",
        "screenW+10-areaH/2-(randS*areaH/2)/120", NULL
    };
    if (valid_spawn_expression(text)) return TRUE;
    for (int i = 0; exact[i]; i++)
        if (strcmp(text, exact[i]) == 0) return TRUE;
    return FALSE;
}

static gboolean child_graph_has_cycle(const EsheepPetPackage *package,
                                      int animation_id, int path[],
                                      int path_length) {
    if (path_length > ESHEEP_RENDER_MAX_CHILDREN - 1) return TRUE;
    for (int i = 0; i < path_length; i++)
        if (path[i] == animation_id) return TRUE;
    path[path_length++] = animation_id;
    for (int i = 0; i < package->child_count; i++) {
        if (package->childs[i].animation_id != animation_id) continue;
        if (child_graph_has_cycle(package, package->childs[i].next, path,
                                  path_length)) return TRUE;
    }
    return FALSE;
}

static AnimationBuild *new_animation(EsheepPetPackage *package, int id) {
    AnimationBuild *build = g_new0(AnimationBuild, 1);
    build->value.id = id;
    build->value.name = owned_string(package, "");
    build->value.start.x = owned_string(package, "0");
    build->value.start.y = owned_string(package, "0");
    build->value.start.interval_ms = 100;
    build->value.start.offsety = owned_string(package, "0");
    build->value.start.opacity = 1.0;
    build->value.end = build->value.start;
    build->value.repeat = owned_string(package, "0");
    build->value.repeat_from = owned_string(package, "0");
    build->frames = g_array_new(FALSE, FALSE, sizeof(int));
    build->sequence_next = g_array_new(FALSE, FALSE, sizeof(EsheepTransition));
    build->border_next = g_array_new(FALSE, FALSE, sizeof(EsheepTransition));
    build->gravity_next = g_array_new(FALSE, FALSE, sizeof(EsheepTransition));
    g_ptr_array_add(package->animation_builds, build);
    return build;
}

static SpawnBuild *new_spawn(EsheepPetPackage *package, int id, int probability) {
    SpawnBuild *build = g_new0(SpawnBuild, 1);
    build->value.id = id;
    build->value.probability = probability;
    build->value.x = owned_string(package, "0");
    build->value.y = owned_string(package, "0");
    build->next = g_array_new(FALSE, FALSE, sizeof(EsheepTransition));
    g_ptr_array_add(package->spawn_builds, build);
    return build;
}

static void begin_field(ParseState *state, const char *name) {
    state->field = FIELD_NONE;
    if (state->in_child) {
        if (strcmp(name, "x") == 0) state->field = FIELD_CHILD_X;
        else if (strcmp(name, "y") == 0) state->field = FIELD_CHILD_Y;
        else if (strcmp(name, "next") == 0) state->field = FIELD_CHILD_NEXT;
    } else if (state->animation) {
        if (strcmp(name, "name") == 0) state->field = FIELD_ANIMATION_NAME;
        else if (strcmp(name, "repeat") == 0) state->field = FIELD_REPEAT;
        else if (strcmp(name, "repeatfrom") == 0) state->field = FIELD_REPEAT_FROM;
        else if (strcmp(name, "frame") == 0) state->field = FIELD_FRAME;
        else if (strcmp(name, "x") == 0) state->field = FIELD_POSE_X;
        else if (strcmp(name, "y") == 0) state->field = FIELD_POSE_Y;
        else if (strcmp(name, "interval") == 0) state->field = FIELD_POSE_INTERVAL;
        else if (strcmp(name, "offsety") == 0) state->field = FIELD_POSE_OFFSET_Y;
        else if (strcmp(name, "opacity") == 0) state->field = FIELD_POSE_OPACITY;
    } else if (state->spawn) {
        if (strcmp(name, "x") == 0) state->field = FIELD_SPAWN_X;
        else if (strcmp(name, "y") == 0) state->field = FIELD_SPAWN_Y;
    } else if (strcmp(name, "tilesx") == 0) state->field = FIELD_HEADER_TILES_X;
    else if (strcmp(name, "tilesy") == 0) state->field = FIELD_HEADER_TILES_Y;
    else if (strcmp(name, "file") == 0 || strcmp(name, "spritesheet") == 0)
        state->field = FIELD_IMAGE_FILE;
    if (state->field != FIELD_NONE) {
        if (state->text) g_string_truncate(state->text, 0);
        else state->text = g_string_new(NULL);
    }
}

static void start_element(GMarkupParseContext *context, const gchar *element,
                          const gchar **attribute_names,
                          const gchar **attribute_values, gpointer user_data,
                          GError **error) {
    ParseState *state = user_data;
    (void)context;
    state->error = error;
    if (strcmp(element, "animation") == 0) {
        int id;
        const char *raw = NULL;
        for (int i = 0; attribute_names && attribute_names[i]; i++)
            if (strcmp(attribute_names[i], "id") == 0) raw = attribute_values[i];
        if (!parse_int(raw, &id) || id < 1) { set_error(state, "animation id must be a positive integer"); return; }
        state->animation = new_animation(state->package, id);
    } else if (strcmp(element, "spawn") == 0) {
        int id, probability = 100;
        const char *raw_id = NULL;
        for (int i = 0; attribute_names && attribute_names[i]; i++) {
            if (strcmp(attribute_names[i], "id") == 0) raw_id = attribute_values[i];
            if (strcmp(attribute_names[i], "probability") == 0 &&
                !parse_int(attribute_values[i], &probability))
                set_error(state, "spawn probability must be an integer");
        }
        if (!parse_int(raw_id, &id) || id < 1 || probability < 1 || probability > 100) {
            set_error(state, "invalid spawn attributes"); return;
        }
        state->spawn = new_spawn(state->package, id, probability);
    } else if (strcmp(element, "child") == 0) {
        int id;
        const char *raw = NULL;
        for (int i = 0; attribute_names && attribute_names[i]; i++)
            if (strcmp(attribute_names[i], "animationid") == 0) raw = attribute_values[i];
        if (!parse_int(raw, &id) || id < 1) { set_error(state, "child parent id must be positive"); return; }
        memset(&state->child, 0, sizeof(state->child));
        state->child.animation_id = id;
        state->child.x = owned_string(state->package, "0");
        state->child.y = owned_string(state->package, "0");
        state->in_child = TRUE;
    } else if (strcmp(element, "start") == 0) {
        if (state->animation) state->animation->has_start = TRUE;
        state->in_start_pose = TRUE;
    } else if (strcmp(element, "end") == 0) {
        if (state->animation) state->animation->has_end = TRUE;
        state->in_end_pose = TRUE;
    }
    else if (state->in_child && strcmp(element, "next") == 0) {
        begin_field(state, element);
    } else if (strcmp(element, "sequence") == 0) {
        if (state->animation) {
            state->animation->has_sequence = TRUE;
            state->transition_destination = state->animation->sequence_next;
            for (int i = 0; attribute_names && attribute_names[i]; i++) {
                if (strcmp(attribute_names[i], "repeat") == 0)
                    state->animation->value.repeat = owned_string(state->package, attribute_values[i]);
                else if (strcmp(attribute_names[i], "repeatfrom") == 0)
                    state->animation->value.repeat_from = owned_string(state->package, attribute_values[i]);
            }
        }
    } else if (strcmp(element, "border") == 0) {
        if (state->animation) state->transition_destination = state->animation->border_next;
    } else if (strcmp(element, "gravity") == 0) {
        if (state->animation) state->transition_destination = state->animation->gravity_next;
    } else if (strcmp(element, "action") == 0) {
        if (state->animation) state->field = FIELD_ACTION;
        if (state->text) g_string_truncate(state->text, 0);
        else state->text = g_string_new(NULL);
    } else if (strcmp(element, "next") == 0) {
        PendingNext *pending = g_new0(PendingNext, 1);
        int probability = 100;
        for (int i = 0; attribute_names && attribute_names[i]; i++) {
            if (strcmp(attribute_names[i], "probability") == 0 &&
                !parse_int(attribute_values[i], &probability))
                set_error(state, "transition probability must be an integer");
            if (strcmp(attribute_names[i], "only") == 0) pending->only = g_strdup(attribute_values[i]);
        }
        pending->transition.probability = probability;
        pending->text = g_string_new(NULL);
        state->pending_next = pending;
    } else begin_field(state, element);
}

static void text_data(GMarkupParseContext *context, const gchar *text,
                      gsize length, gpointer user_data, GError **error) {
    ParseState *state = user_data;
    (void)context;
    state->error = error;
    if (state->pending_next) g_string_append_len(state->pending_next->text, text, length);
    else if (state->text) g_string_append_len(state->text, text, length);
}

static GArray *transition_destination(ParseState *state) {
    if (state->animation) {
        /* The current section is identified by whether the parser is inside
         * sequence, border, or gravity through the element stack below. */
        return state->transition_destination ? state->transition_destination :
               state->animation->sequence_next;
    }
    return state->spawn ? state->spawn->next : NULL;
}

static void finish_text(ParseState *state) {
    if (!state->text || !state->text->len) return;
    const char *value = g_strstrip(state->text->str);
    int integer;
    AnimationBuild *a = state->animation;
    if (state->field == FIELD_HEADER_TILES_X || state->field == FIELD_HEADER_TILES_Y) {
        if (!parse_int(value, state->field == FIELD_HEADER_TILES_X ? &state->package->tiles_x : &state->package->tiles_y))
            set_error(state, "tile dimensions must be integers");
    } else if (state->in_child) {
        if (state->field == FIELD_CHILD_X) state->child.x = owned_string(state->package, value);
        else if (state->field == FIELD_CHILD_Y) state->child.y = owned_string(state->package, value);
        else if (state->field == FIELD_CHILD_NEXT && (!parse_int(value, &integer) || integer < 1)) set_error(state, "child target must be positive");
        else if (state->field == FIELD_CHILD_NEXT) state->child.next = integer;
    } else if (a) {
        EsheepPose *pose = state->in_end_pose ? &a->value.end : &a->value.start;
        if (state->field == FIELD_ANIMATION_NAME) a->value.name = owned_string(state->package, value);
        else if (state->field == FIELD_POSE_X) pose->x = owned_string(state->package, value);
        else if (state->field == FIELD_POSE_Y) pose->y = owned_string(state->package, value);
        else if (state->field == FIELD_POSE_INTERVAL && (!parse_int(value, &integer) || integer < 1)) set_error(state, "pose interval must be positive");
        else if (state->field == FIELD_POSE_INTERVAL) pose->interval_ms = integer;
        else if (state->field == FIELD_POSE_OFFSET_Y) pose->offsety = owned_string(state->package, value);
        else if (state->field == FIELD_POSE_OPACITY && !parse_double(value, &pose->opacity)) set_error(state, "pose opacity must be numeric");
        else if (state->field == FIELD_REPEAT) a->value.repeat = owned_string(state->package, value);
        else if (state->field == FIELD_REPEAT_FROM) a->value.repeat_from = owned_string(state->package, value);
        else if (state->field == FIELD_ACTION && strcmp(value, "flip") == 0) a->value.flip = 1;
        else if (state->field == FIELD_ACTION) set_error(state, "unsupported animation action");
        else if (state->field == FIELD_FRAME && (!parse_int(value, &integer) || integer < 0)) set_error(state, "frame must be a non-negative integer");
        else if (state->field == FIELD_FRAME) g_array_append_val(a->frames, integer);
    } else if (state->spawn) {
        if (state->field == FIELD_SPAWN_X) state->spawn->value.x = owned_string(state->package, value);
        else if (state->field == FIELD_SPAWN_Y) state->spawn->value.y = owned_string(state->package, value);
    } else if (state->field == FIELD_IMAGE_FILE) {
        state->package->spritesheet = owned_string(state->package, value);
    }
}

static void end_element(GMarkupParseContext *context, const gchar *element,
                        gpointer user_data, GError **error) {
    ParseState *state = user_data;
    (void)context;
    state->error = error;
    if (state->pending_next && strcmp(element, "next") == 0) {
        int target;
        char *value = g_strstrip(state->pending_next->text->str);
        if (!parse_int(value, &target) || target < 1) set_error(state, "transition target must be positive");
        state->pending_next->transition.target = target;
        state->pending_next->transition.only = state->pending_next->only ?
            owned_string(state->package, state->pending_next->only) : NULL;
        GArray *destination = transition_destination(state);
        if (destination) g_array_append_val(destination, state->pending_next->transition);
        g_free(state->pending_next->only);
        g_string_free(state->pending_next->text, TRUE);
        g_free(state->pending_next);
        state->pending_next = NULL;
    } else {
        finish_text(state);
        if (state->field == FIELD_FRAME && state->animation) state->field = FIELD_NONE;
        if (strcmp(element, "start") == 0) state->in_start_pose = FALSE;
        if (strcmp(element, "end") == 0) state->in_end_pose = FALSE;
        if (strcmp(element, "child") == 0) {
            g_array_append_val(state->package->child_builds, state->child);
            state->in_child = FALSE;
        }
        if (strcmp(element, "animation") == 0) state->animation = NULL;
        if (strcmp(element, "spawn") == 0) state->spawn = NULL;
        if (strcmp(element, "sequence") == 0 || strcmp(element, "border") == 0 ||
            strcmp(element, "gravity") == 0) state->transition_destination = NULL;
        state->field = FIELD_NONE;
    }
}

static gboolean validate_package(EsheepPetPackage *package, GError **error) {
    if (package->tiles_x < 1 || package->tiles_y < 1 || package->tiles_x > 256 || package->tiles_y > 256)
        return g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT, "tile grid must be between 1 and 256"), FALSE;
    package->animation_count = (int)package->animation_builds->len;
    if (package->animation_count < 1) return g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT, "package has no animations"), FALSE;
    package->animations = g_new0(EsheepAnimation, package->animation_count);
    for (int i = 0; i < package->animation_count; i++) {
        AnimationBuild *build = g_ptr_array_index(package->animation_builds, i);
        if (build->value.id != i + 1 || !build->has_start || !build->has_end ||
            !build->has_sequence || build->frames->len == 0)
            return g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT, "animation IDs must be contiguous and every animation needs start, end, and sequence data"), FALSE;
        if (!valid_integer_or_image_factor(build->value.start.x) || !valid_integer_or_image_factor(build->value.start.y) ||
            !valid_integer_or_image_factor(build->value.end.x) || !valid_integer_or_image_factor(build->value.end.y) ||
            !valid_repeat_expression(build->value.repeat) || !valid_repeat_expression(build->value.repeat_from))
            return g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT, "unsupported animation expression"), FALSE;
        if (build->value.start.opacity < 0.0 || build->value.start.opacity > 1.0 ||
            build->value.end.opacity < 0.0 || build->value.end.opacity > 1.0)
            return g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT, "pose opacity must be between 0 and 1"), FALSE;
        GArray *transition_lists[] = { build->sequence_next, build->border_next,
                                       build->gravity_next };
        for (guint list = 0; list < G_N_ELEMENTS(transition_lists); list++) {
            GArray *transitions = transition_lists[list];
            for (guint j = 0; j < transitions->len; j++) {
                EsheepTransition transition = g_array_index(transitions, EsheepTransition, j);
                if (transition.probability < 1 || transition.probability > 100 ||
                    transition.target < 1 || transition.target > package->animation_count ||
                    (transition.only && strcmp(transition.only, "none") != 0 &&
                     strcmp(transition.only, "window") != 0 &&
                     strcmp(transition.only, "taskbar") != 0 &&
                     strcmp(transition.only, "vertical") != 0 &&
                     strcmp(transition.only, "horizontal+") != 0))
                    return g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT, "invalid animation transition"), FALSE;
            }
        }
        for (guint j = 0; j < build->frames->len; j++) {
            int frame = g_array_index(build->frames, int, j);
            if (frame >= package->tiles_x * package->tiles_y) return g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT, "animation frame exceeds tile grid"), FALSE;
        }
        package->animations[i] = build->value;
        package->animations[i].frames = (const int *)build->frames->data;
        package->animations[i].frame_count = (int)build->frames->len;
        package->animations[i].sequence_next = (const EsheepTransition *)build->sequence_next->data;
        package->animations[i].sequence_next_count = (int)build->sequence_next->len;
        package->animations[i].border_next = (const EsheepTransition *)build->border_next->data;
        package->animations[i].border_next_count = (int)build->border_next->len;
        package->animations[i].gravity_next = (const EsheepTransition *)build->gravity_next->data;
        package->animations[i].gravity_next_count = (int)build->gravity_next->len;
    }
    package->spawn_count = (int)package->spawn_builds->len;
    package->spawns = g_new0(EsheepSpawn, package->spawn_count);
    for (int i = 0; i < package->spawn_count; i++) {
        SpawnBuild *build = g_ptr_array_index(package->spawn_builds, i);
        if (build->value.id != i + 1 || build->value.probability < 1 ||
            build->value.probability > 100 ||
            !valid_spawn_expression(build->value.x) ||
            !valid_spawn_expression(build->value.y))
            return g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT, "invalid spawn definition"), FALSE;
        for (guint j = 0; j < build->next->len; j++) {
            EsheepTransition transition = g_array_index(build->next, EsheepTransition, j);
            if (transition.probability < 1 || transition.probability > 100 ||
                transition.target < 1 || transition.target > package->animation_count ||
                (transition.only && strcmp(transition.only, "none") != 0 &&
                 strcmp(transition.only, "window") != 0 &&
                 strcmp(transition.only, "taskbar") != 0 &&
                 strcmp(transition.only, "vertical") != 0 &&
                 strcmp(transition.only, "horizontal+") != 0))
                return g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT, "invalid spawn transition"), FALSE;
        }
        package->spawns[i] = build->value;
        package->spawns[i].next = (const EsheepTransition *)build->next->data;
        package->spawns[i].next_count = (int)build->next->len;
    }
    package->child_count = (int)package->child_builds->len;
    if (package->child_count > ESHEEP_RENDER_MAX_CHILDREN - 1)
        return g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT, "package has more child records than the renderer can compose"), FALSE;
    package->childs = g_new0(EsheepChild, package->child_count);
    for (int i = 0; i < package->child_count; i++) {
        EsheepChild child = g_array_index(package->child_builds, EsheepChild, i);
        if (child.animation_id < 1 || child.animation_id > package->animation_count ||
            child.next < 1 || child.next > package->animation_count ||
            !valid_child_expression(child.x) || !valid_child_expression(child.y))
            return g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT, "invalid child definition"), FALSE;
        package->childs[i] = child;
    }
    /* Multiple child records may share a parent. Reject cycles so a package
     * cannot create an unbounded child tree when the GTK host materializes it. */
    for (int i = 0; i < package->child_count; i++) {
        int parent = package->childs[i].animation_id;
        int path[ESHEEP_PACKAGE_MAX_CHILD_DEPTH + 1];
        if (child_graph_has_cycle(package, parent, path, 0))
            return g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT, "child nesting exceeds runtime depth"), FALSE;
    }
    return TRUE;
}

gboolean esheep_pet_package_load(const char *path, EsheepPetPackage **out,
                                 GError **error) {
    gchar *contents = NULL;
    gsize length = 0;
    if (!out || !path || !g_file_get_contents(path, &contents, &length, error)) return FALSE;
    EsheepPetPackage *package = g_new0(EsheepPetPackage, 1);
    package->tiles_x = 16; package->tiles_y = 11;
    package->strings = g_ptr_array_new_with_free_func(g_free);
    package->animation_builds = g_ptr_array_new();
    package->spawn_builds = g_ptr_array_new();
    package->child_builds = g_array_new(FALSE, FALSE, sizeof(EsheepChild));
    ParseState state = { .package = package, .error = error };
    GMarkupParser parser = { start_element, end_element, text_data, NULL, NULL };
    GMarkupParseContext *context = g_markup_parse_context_new(&parser, G_MARKUP_TREAT_CDATA_AS_TEXT, &state, NULL);
    gboolean ok = g_markup_parse_context_parse(context, contents, length, error) &&
                  g_markup_parse_context_end_parse(context, error) && validate_package(package, error);
    g_markup_parse_context_free(context);
    g_free(contents);
    if (!ok) { esheep_pet_package_free(package); return FALSE; }
    if (package->spritesheet && package->spritesheet[0] &&
        !g_path_is_absolute(package->spritesheet)) {
        gchar *directory = g_path_get_dirname(path);
        gchar *absolute = g_canonicalize_filename(package->spritesheet,
                                                  directory);
        package->spritesheet = owned_string(package, absolute);
        g_free(absolute);
        g_free(directory);
    }
    *out = package;
    return TRUE;
}

void esheep_pet_package_activate(EsheepPetPackage *package) {
    if (!package) return;
    esheep_tiles_x = package->tiles_x; esheep_tiles_y = package->tiles_y;
    esheep_spawns = package->spawns; esheep_spawn_count = package->spawn_count;
    esheep_animations = package->animations; esheep_animation_count = package->animation_count;
    esheep_childs = package->childs; esheep_child_count = package->child_count;
}

const char *esheep_pet_package_spritesheet(const EsheepPetPackage *package) {
    return package ? package->spritesheet : NULL;
}

void esheep_pet_package_free(EsheepPetPackage *package) {
    if (!package) return;
    esheep_use_default_animation_data();
    if (package->animation_builds) {
        for (guint i = 0; i < package->animation_builds->len; i++) {
            AnimationBuild *build = g_ptr_array_index(package->animation_builds, i);
            g_array_free(build->frames, TRUE); g_array_free(build->sequence_next, TRUE);
            g_array_free(build->border_next, TRUE); g_array_free(build->gravity_next, TRUE);
            g_free(build);
        }
        g_ptr_array_free(package->animation_builds, TRUE);
    }
    if (package->spawn_builds) {
        for (guint i = 0; i < package->spawn_builds->len; i++) {
            SpawnBuild *build = g_ptr_array_index(package->spawn_builds, i);
            g_array_free(build->next, TRUE); g_free(build);
        }
        g_ptr_array_free(package->spawn_builds, TRUE);
    }
    if (package->child_builds) g_array_free(package->child_builds, TRUE);
    if (package->strings) g_ptr_array_free(package->strings, TRUE);
    g_free(package->animations); g_free(package->spawns); g_free(package->childs);
    g_free(package);
}
