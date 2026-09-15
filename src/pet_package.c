#include "pet_package.h"
#include "animations_data.h"
#include "expression.h"
#include "renderer.h"
#include <glib/gstdio.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ESHEEP_PACKAGE_MAX_CHILD_DEPTH 4

static guint32 transition_stable_id(int source_id, int event_kind,
                                    int ordinal, int target) {
    return ((guint32)(source_id & 0xff) << 24) |
           ((guint32)(event_kind & 0x0f) << 20) |
           ((guint32)(ordinal & 0xff) << 12) |
           (guint32)(target & 0xfff);
}

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
    FIELD_IMAGE_TILES_X,
    FIELD_IMAGE_TILES_Y,
    FIELD_IMAGE_TRANSPARENCY,
    FIELD_IMAGE_FILE,
    FIELD_IMAGE_SPRITESHEET,
    FIELD_IMAGE_PNG,
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
    FIELD_SOUND_ANIMATION_ID,
    FIELD_SOUND_PROBABILITY,
    FIELD_SOUND_LOOP_COUNT,
    FIELD_SOUND_PAYLOAD,
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
    EsheepPackageImage *image;
    GPtrArray *sounds;
    int sound_count;
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

static EsheepPetPackage *active_package;

typedef struct {
    EsheepPetPackage *package;
    gboolean in_image;
    gboolean in_sounds;
    EsheepPackageSound *current_sound;
    AnimationBuild *animation;
    SpawnBuild *spawn;
    EsheepChild child;
    gboolean in_child;
    gboolean in_start_pose;
    gboolean in_end_pose;
    gboolean in_image_png;
    Field field;
    PendingNext *pending_next;
    GString *text;
    GError **error;
    GArray *transition_destination;
} ParseState;

static void parse_state_clear(ParseState *state) {
    if (state->text) {
        g_string_free(state->text, TRUE);
        state->text = NULL;
    }
    if (state->pending_next) {
        g_free(state->pending_next->only);
        g_string_free(state->pending_next->text, TRUE);
        g_free(state->pending_next);
        state->pending_next = NULL;
    }
    if (state->current_sound) {
        g_free(state->current_sound);
        state->current_sound = NULL;
    }
}

static void set_error(ParseState *state, const char *message) {
    if (state->error && !*state->error)
        g_set_error(state->error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                    "%s", message);
}

static gboolean parse_base64(const char *text, guchar **out_data, gsize *out_size) {
    if (!text || !*text) return FALSE;
    GString *clean = g_string_new(NULL);
    gboolean padding = FALSE;
    int padding_count = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if (g_ascii_isspace(*p)) continue;
        if (*p == '=') {
            padding = TRUE;
            padding_count++;
            if (padding_count > 2) { g_string_free(clean, TRUE); return FALSE; }
        } else {
            if (padding || (!g_ascii_isalnum(*p) && *p != '+' && *p != '/')) {
                g_string_free(clean, TRUE); return FALSE;
            }
        }
        g_string_append_c(clean, (char)*p);
    }
    if (clean->len == 0 || clean->len % 4 != 0 ||
        (padding_count && clean->len - padding_count < 2)) {
        g_string_free(clean, TRUE);
        return FALSE;
    }
    guchar *decoded = g_base64_decode(clean->str, out_size);
    g_string_free(clean, TRUE);
    if (!decoded) return FALSE;
    if (*out_size == 0) {
        g_free(decoded);
        return FALSE;
    }
    *out_data = decoded;
    return TRUE;
}

static char *trim_whitespace(char *str) {
    char *end;
    while (*str && g_ascii_isspace(*str)) str++;
    if (!*str) return str;
    end = str + strlen(str) - 1;
    while (end > str && g_ascii_isspace(*end)) end--;
    *(end + 1) = '\0';
    return str;
}

static gboolean parse_int(const char *text, int *out) {
    char *end = NULL;
    long value;
    if (!text || !*text) return FALSE;
    errno = 0;
    value = strtol(text, &end, 10);
    if (errno || end == text || *end != '\0' || value < INT_MIN ||
        value > INT_MAX) return FALSE;
    if (out) *out = (int)value;
    return TRUE;
}

static gboolean parse_double(const char *text, double *out) {
    char *end = NULL;
    double value;
    if (!text || !*text) return FALSE;
    errno = 0;
    value = g_ascii_strtod(text, &end);
    if (errno || end == text || *end != '\0' || !isfinite(value)) return FALSE;
    if (out) *out = value;
    return TRUE;
}

/* Evaluate against a deterministic, non-zero context so package expressions
 * are known to produce a finite value that fits the runtime integer fields.
 * Runtime evaluation repeats this check with the live monitor dimensions. */
static gboolean evaluate_int_expression(const char *text, int *result) {
    EsheepExpressionContext context = { 1, 1, 1, 1, 1, 1, 0, 0, 0 };
    return esheep_expression_eval_int(text, &context, result);
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

static char *owned_string(EsheepPetPackage *package, const char *value);

static gint compare_animation_builds(gconstpointer left, gconstpointer right) {
    const AnimationBuild *a = *(const AnimationBuild *const *)left;
    const AnimationBuild *b = *(const AnimationBuild *const *)right;
    return a->value.id - b->value.id;
}

static gint compare_spawn_builds(gconstpointer left, gconstpointer right) {
    const SpawnBuild *a = *(const SpawnBuild *const *)left;
    const SpawnBuild *b = *(const SpawnBuild *const *)right;
    return a->value.id - b->value.id;
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
    if (state->in_image) {
        if (strcmp(name, "tilesx") == 0) state->field = FIELD_IMAGE_TILES_X;
        else if (strcmp(name, "tilesy") == 0) state->field = FIELD_IMAGE_TILES_Y;
        else if (strcmp(name, "transparency") == 0) state->field = FIELD_IMAGE_TRANSPARENCY;
        else if (strcmp(name, "file") == 0) state->field = FIELD_IMAGE_FILE;
        else if (strcmp(name, "spritesheet") == 0) state->field = FIELD_IMAGE_SPRITESHEET;
        else if (strcmp(name, "png") == 0) {
            state->field = FIELD_IMAGE_PNG;
            state->in_image_png = TRUE;
        }
    } else if (state->in_sounds) {
        if (strcmp(name, "animationid") == 0) state->field = FIELD_SOUND_ANIMATION_ID;
        else if (strcmp(name, "probability") == 0) state->field = FIELD_SOUND_PROBABILITY;
        else if (strcmp(name, "loop") == 0) state->field = FIELD_SOUND_LOOP_COUNT;
        else if (strcmp(name, "base64") == 0) state->field = FIELD_SOUND_PAYLOAD;
    } else if (state->in_child) {
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
    } else if (strcmp(element, "image") == 0) {
        state->in_image = TRUE;
        if (!state->package->image) {
            state->package->image = g_new0(EsheepPackageImage, 1);
            state->package->image->tiles_x = state->package->tiles_x;
            state->package->image->tiles_y = state->package->tiles_y;
            state->package->image->transparency = ESHEEP_TRANSPARENCY_NONE;
        }
    } else if (strcmp(element, "sounds") == 0) {
        state->in_sounds = TRUE;
        if (!state->package->sounds) {
            state->package->sounds = g_ptr_array_new_with_free_func(g_free);
        }
    } else if (state->in_sounds && strcmp(element, "sound") == 0) {
        state->current_sound = g_new0(EsheepPackageSound, 1);
        state->current_sound->probability = 100;
        state->current_sound->loop_count = 0;
        state->current_sound->payload = NULL;
        state->current_sound->payload_size = 0;
        for (int i = 0; attribute_names && attribute_names[i]; i++) {
            if (strcmp(attribute_names[i], "animationid") == 0 &&
                !parse_int(attribute_values[i], &state->current_sound->animation_id))
                set_error(state, "sound animationid must be positive");
        }
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
    char *value = trim_whitespace(state->text->str);
    int integer;
    AnimationBuild *a = state->animation;
    if (state->field == FIELD_HEADER_TILES_X || state->field == FIELD_HEADER_TILES_Y) {
        if (!parse_int(value, state->field == FIELD_HEADER_TILES_X ? &state->package->tiles_x : &state->package->tiles_y))
            set_error(state, "tile dimensions must be integers");
    } else if (state->in_image) {
        if (!state->package->image) {
            state->package->image = g_new0(EsheepPackageImage, 1);
            state->package->image->tiles_x = state->package->tiles_x;
            state->package->image->tiles_y = state->package->tiles_y;
            state->package->image->transparency = ESHEEP_TRANSPARENCY_NONE;
        }
        if (state->field == FIELD_IMAGE_TILES_X) {
            if (!parse_int(value, &state->package->image->tiles_x) || state->package->image->tiles_x < 1)
                set_error(state, "image tilesx must be positive");
            else
                state->package->tiles_x = state->package->image->tiles_x;
        } else if (state->field == FIELD_IMAGE_TILES_Y) {
            if (!parse_int(value, &state->package->image->tiles_y) || state->package->image->tiles_y < 1)
                set_error(state, "image tilesy must be positive");
            else
                state->package->tiles_y = state->package->image->tiles_y;
        } else if (state->field == FIELD_IMAGE_TRANSPARENCY) {
            if (g_ascii_strcasecmp(value, "None") == 0 || !*value) state->package->image->transparency = ESHEEP_TRANSPARENCY_NONE;
            else if (g_ascii_strcasecmp(value, "Magenta") == 0) state->package->image->transparency = ESHEEP_TRANSPARENCY_MAGENTA;
            else if (g_ascii_strcasecmp(value, "Transparent") == 0) state->package->image->transparency = ESHEEP_TRANSPARENCY_TRANSPARENT;
            else if (g_ascii_strcasecmp(value, "Green") == 0) state->package->image->transparency = ESHEEP_TRANSPARENCY_GREEN;
            else if (g_ascii_strcasecmp(value, "Cyan") == 0) state->package->image->transparency = ESHEEP_TRANSPARENCY_CYAN;
            else set_error(state, "transparency must be None, Magenta, Transparent, or Green");
        } else if (state->field == FIELD_IMAGE_FILE || state->field == FIELD_IMAGE_SPRITESHEET) {
            state->package->spritesheet = owned_string(state->package, value);
            if (state->package->image)
                state->package->image->spritesheet_path = (char *)state->package->spritesheet;
        } else if (state->field == FIELD_IMAGE_PNG && state->in_image_png) {
            if (state->package->image) {
                gsize png_size;
                if (parse_base64(value, &state->package->image->png_data, &png_size))
                    state->package->image->png_size = png_size;
                else
                    set_error(state, "invalid base64 PNG data");
            }
        }
    } else if (state->in_sounds && state->current_sound) {
        if (state->field == FIELD_SOUND_ANIMATION_ID) {
            if (!parse_int(value, &integer) || integer < 1)
                set_error(state, "sound animationid must be positive");
            else
                state->current_sound->animation_id = integer;
        } else if (state->field == FIELD_SOUND_PROBABILITY) {
            if (!parse_int(value, &integer) || integer < 1 || integer > 100)
                set_error(state, "sound probability must be 1-100");
            else
                state->current_sound->probability = integer;
        } else if (state->field == FIELD_SOUND_LOOP_COUNT) {
            if (!parse_int(value, &integer) || integer < 0)
                set_error(state, "sound loopcount must be non-negative");
            else
                state->current_sound->loop_count = integer;
        } else if (state->field == FIELD_SOUND_PAYLOAD) {
            gsize payload_size;
            if (parse_base64(value, &state->current_sound->payload, &payload_size))
                state->current_sound->payload_size = payload_size;
            else
                set_error(state, "invalid base64 sound payload");
        }
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
        else if (state->field == FIELD_POSE_OFFSET_Y && !parse_int(value, &integer))
            set_error(state, "pose offset must be a representable integer");
        else if (state->field == FIELD_POSE_OFFSET_Y) pose->offsety = owned_string(state->package, value);
        else if (state->field == FIELD_POSE_OPACITY && !parse_double(value, &pose->opacity)) set_error(state, "pose opacity must be numeric");
        else if (state->field == FIELD_REPEAT) a->value.repeat = owned_string(state->package, value);
        else if (state->field == FIELD_REPEAT_FROM) a->value.repeat_from = owned_string(state->package, value);
        else if (state->field == FIELD_ACTION && strcmp(value, "flip") == 0) a->value.flip = 1;
        else if (state->field == FIELD_ACTION && strcmp(value, "none") == 0) { /* no-op */ }
        else if (state->field == FIELD_ACTION) set_error(state, "unsupported animation action");
        else if (state->field == FIELD_FRAME && (!parse_int(value, &integer) || integer < 0)) set_error(state, "frame index must be a non-negative representable integer");
        else if (state->field == FIELD_FRAME) g_array_append_val(a->frames, integer);
    } else if (state->spawn) {
        if (state->field == FIELD_SPAWN_X) state->spawn->value.x = owned_string(state->package, value);
        else if (state->field == FIELD_SPAWN_Y) state->spawn->value.y = owned_string(state->package, value);
    } else if (state->field == FIELD_IMAGE_FILE) {
        state->package->spritesheet = owned_string(state->package, value);
        if (state->package->image)
            state->package->image->spritesheet_path = owned_string(state->package, value);
    }
}

static void end_element(GMarkupParseContext *context, const gchar *element,
                        gpointer user_data, GError **error) {
    ParseState *state = user_data;
    (void)context;
    state->error = error;
    if (state->pending_next && strcmp(element, "next") == 0) {
        int target;
        char *value = trim_whitespace(state->pending_next->text->str);
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
        if (strcmp(element, "image") == 0) state->in_image = FALSE;
        if (strcmp(element, "sounds") == 0) state->in_sounds = FALSE;
        if (state->in_sounds && strcmp(element, "sound") == 0 && state->current_sound) {
            if (state->current_sound->animation_id < 1)
                set_error(state, "sound requires animationid");
            else if (!state->current_sound->payload)
                set_error(state, "sound requires payload");
            else {
                g_ptr_array_add(state->package->sounds, state->current_sound);
            }
            state->current_sound = NULL;
        }
        if (strcmp(element, "png") == 0) state->in_image_png = FALSE;
        state->field = FIELD_NONE;
    }
}

static char *owned_string(EsheepPetPackage *package, const char *value) {
    char *copy = g_strdup(value ? value : "");
    g_ptr_array_add(package->strings, copy);
    return copy;
}

static gboolean validate_package(EsheepPetPackage *package, GError **error) {
    if (package->tiles_x < 1 || package->tiles_y < 1 || package->tiles_x > 256 || package->tiles_y > 256)
        return g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT, "tile grid must be between 1 and 256"), FALSE;
    g_ptr_array_sort(package->animation_builds, compare_animation_builds);
    package->animation_count = (int)package->animation_builds->len;
    if (package->animation_count < 1) return g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT, "package has no animations"), FALSE;
    package->animations = g_new0(EsheepAnimation, package->animation_count);
    for (int i = 0; i < package->animation_count; i++) {
        AnimationBuild *build = g_ptr_array_index(package->animation_builds, i);
        if (build->value.id != i + 1 || !build->has_start || !build->has_end ||
            !build->has_sequence || build->frames->len == 0)
            return g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT, "animation %d invalid: id=%d start=%d end=%d sequence=%d frames=%u", i + 1, build->value.id, build->has_start, build->has_end, build->has_sequence, build->frames->len), FALSE;
        const char *pose_int_fields[] = {
            build->value.start.x, build->value.start.y,
            build->value.end.x, build->value.end.y
        };
        for (guint k = 0; k < G_N_ELEMENTS(pose_int_fields); k++) {
            int dummy;
            if (!pose_int_fields[k] || !*pose_int_fields[k]) {
                                continue;
            }
                        if (!evaluate_int_expression(pose_int_fields[k], &dummy)) {
                                return g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                                   "animation pose expression must be a finite 32-bit integer"), FALSE;
            }
                    }
        const char *repeat_int_fields[] = {
            build->value.repeat, build->value.repeat_from
        };
        for (guint k = 0; k < G_N_ELEMENTS(repeat_int_fields); k++) {
            if (!repeat_int_fields[k] || !*repeat_int_fields[k]) {
                                continue;
            }
            int dummy;
                        if (!evaluate_int_expression(repeat_int_fields[k], &dummy)) {
                                return g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                                   "animation repeat expression must be a finite 32-bit integer"), FALSE;
            }
                    }
        if (build->value.start.opacity < 0.0 || build->value.start.opacity > 1.0 ||
            build->value.end.opacity < 0.0 || build->value.end.opacity > 1.0)
            return g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT, "pose opacity must be between 0 and 1"), FALSE;
        GArray *transition_lists[] = { build->sequence_next, build->border_next,
                                       build->gravity_next };
        for (guint list = 0; list < G_N_ELEMENTS(transition_lists); list++) {
            GArray *transitions = transition_lists[list];
            for (guint j = 0; j < transitions->len; j++) {
                EsheepTransition transition = g_array_index(transitions, EsheepTransition, j);
                transition.stable_id = transition_stable_id(
                    build->value.id, (int)list, (int)j, transition.target);
                if (transition.probability < 0 || transition.probability > 100 ||
                    transition.target < 1 || transition.target > package->animation_count ||
                    (transition.only && strcmp(transition.only, "none") != 0 &&
                     strcmp(transition.only, "window") != 0 &&
                     strcmp(transition.only, "taskbar") != 0 &&
                     strcmp(transition.only, "vertical") != 0 &&
                     strcmp(transition.only, "horizontal") != 0 &&
                     strcmp(transition.only, "horizontal+") != 0))
                    return g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT, "invalid animation transition"), FALSE;
                g_array_index(transitions, EsheepTransition, j) = transition;
            }
        }
        for (guint j = 0; j < build->frames->len; j++) {
            int frame = g_array_index(build->frames, int, j);
            if (frame >= package->tiles_x * package->tiles_y)
                return g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT, "animation frame exceeds tile grid"), FALSE;
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
    g_ptr_array_sort(package->spawn_builds, compare_spawn_builds);
    package->spawn_count = (int)package->spawn_builds->len;
    package->spawns = g_new0(EsheepSpawn, package->spawn_count);
    for (int i = 0; i < package->spawn_count; i++) {
        SpawnBuild *build = g_ptr_array_index(package->spawn_builds, i);
        if (build->value.id != i + 1 || build->value.probability < 1 ||
            build->value.probability > 100)
            return g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT, "invalid spawn definition"), FALSE;
        const char *spawn_int_fields[] = { build->value.x, build->value.y };
        for (guint k = 0; k < G_N_ELEMENTS(spawn_int_fields); k++) {
            int dummy;
            if (!evaluate_int_expression(spawn_int_fields[k], &dummy))
                return g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                                   "spawn position expression must be a finite 32-bit integer"), FALSE;
        }
        for (guint j = 0; j < build->next->len; j++) {
            EsheepTransition transition = g_array_index(build->next, EsheepTransition, j);
            transition.stable_id = transition_stable_id(build->value.id, 3,
                                                         (int)j, transition.target);
            if (transition.probability < 0 || transition.probability > 100 ||
                transition.target < 1 || transition.target > package->animation_count ||
                (transition.only && strcmp(transition.only, "none") != 0 &&
                 strcmp(transition.only, "window") != 0 &&
                 strcmp(transition.only, "taskbar") != 0 &&
                 strcmp(transition.only, "vertical") != 0 &&
                 strcmp(transition.only, "horizontal") != 0 &&
                 strcmp(transition.only, "horizontal+") != 0))
                return g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT, "invalid spawn transition"), FALSE;
            g_array_index(build->next, EsheepTransition, j) = transition;
        }
        package->spawns[i] = build->value;
        package->spawns[i].next = (const EsheepTransition *)build->next->data;
        package->spawns[i].next_count = (int)build->next->len;
    }
    package->child_count = (int)package->child_builds->len;
    package->childs = g_new0(EsheepChild, package->child_count);
    for (int i = 0; i < package->child_count; i++) {
        EsheepChild child = g_array_index(package->child_builds, EsheepChild, i);
        if (child.animation_id < 1 || child.animation_id > package->animation_count ||
            child.next < 1 || child.next > package->animation_count)
            return g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT, "invalid child definition"), FALSE;
        const char *child_int_fields[] = { child.x, child.y };
        for (guint k = 0; k < G_N_ELEMENTS(child_int_fields); k++) {
            int dummy;
            if (!evaluate_int_expression(child_int_fields[k], &dummy)) {
                return g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                                   "child position expression must be a finite 32-bit integer"), FALSE;
            }
        }
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
    package->sound_count = package->sounds ? (int)package->sounds->len : 0;
    return TRUE;
}

gboolean esheep_pet_package_load(const char *path, EsheepPetPackage **out,
                                 GError **error) {
    gchar *contents = NULL;
    gsize length = 0;
    if (out) *out = NULL;
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
    const gchar *xml = contents;
    gsize xml_length = length;
    if (length >= 3 && (guchar)contents[0] == 0xef &&
        (guchar)contents[1] == 0xbb && (guchar)contents[2] == 0xbf) {
        xml += 3;
        xml_length -= 3;
    }
    gboolean ok = g_markup_parse_context_parse(context, xml, xml_length, error) &&
                  g_markup_parse_context_end_parse(context, error) && validate_package(package, error);
    g_markup_parse_context_free(context);
    g_free(contents);
    if (!ok) {
        parse_state_clear(&state);
        esheep_pet_package_free(package);
        return FALSE;
    }
    parse_state_clear(&state);
    if (package->spritesheet && package->spritesheet[0] &&
        !g_path_is_absolute(package->spritesheet)) {
        gchar *directory = g_path_get_dirname(path);
        gchar *absolute = g_canonicalize_filename(package->spritesheet,
                                                  directory);
        package->spritesheet = owned_string(package, absolute);
        if (package->image)
            package->image->spritesheet_path = (char *)package->spritesheet;
        g_free(absolute);
        g_free(directory);
    }
    *out = package;
    return TRUE;
}

char *esheep_pet_package_resolve_path(const char *path, const char *data_root) {
    if (!path || !*path) return NULL;
    if (g_path_is_absolute(path)) return g_strdup(path);
    if (g_file_test(path, G_FILE_TEST_IS_REGULAR)) return g_strdup(path);
    if (data_root && *data_root) {
        char *resolved = g_build_filename(data_root, path, NULL);
        if (g_file_test(resolved, G_FILE_TEST_IS_REGULAR)) return resolved;
        g_free(resolved);
    }
    return g_strdup(path);
}

void esheep_pet_package_activate(EsheepPetPackage *package) {
    if (!package) {
        active_package = NULL;
        esheep_use_default_animation_data();
        return;
    }
    active_package = package;
    esheep_tiles_x = package->tiles_x; esheep_tiles_y = package->tiles_y;
    esheep_spawns = package->spawns; esheep_spawn_count = package->spawn_count;
    esheep_animations = package->animations; esheep_animation_count = package->animation_count;
    esheep_childs = package->childs; esheep_child_count = package->child_count;
}

const EsheepPackageImage *esheep_pet_package_image(const EsheepPetPackage *package) {
    return package ? package->image : NULL;
}

const EsheepPackageSound *const *esheep_pet_package_sounds(const EsheepPetPackage *package) {
    return package && package->sounds ? (const EsheepPackageSound *const *)package->sounds->pdata : NULL;
}

int esheep_pet_package_sound_count(const EsheepPetPackage *package) {
    return package ? package->sound_count : 0;
}

const char *esheep_pet_package_spritesheet(const EsheepPetPackage *package) {
    return package ? package->spritesheet : NULL;
}

gboolean esheep_pet_package_set_image_grid(EsheepPetPackage *package,
                                           int tiles_x, int tiles_y) {
    if (!package || tiles_x < 1 || tiles_y < 1) return FALSE;
    for (int i = 0; i < package->animation_count; i++) {
        const EsheepAnimation *animation = &package->animations[i];
        for (int frame = 0; frame < animation->frame_count; frame++)
            if (animation->frames[frame] >= tiles_x * tiles_y) return FALSE;
    }
    package->tiles_x = tiles_x;
    package->tiles_y = tiles_y;
    if (package->image) {
        package->image->tiles_x = tiles_x;
        package->image->tiles_y = tiles_y;
    }
    return TRUE;
}

void esheep_pet_package_free(EsheepPetPackage *package) {
    if (!package) return;
        if (active_package == package) {
            esheep_use_default_animation_data();
            active_package = NULL;
        }
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
        if (package->image) {
        // spritesheet_path is owned by package->strings, don't free here
        g_free(package->image->png_data);
        g_free(package->image);
    }
    if (package->sounds) {
        for (guint i = 0; i < package->sounds->len; i++) {
            EsheepPackageSound *sound = g_ptr_array_index(package->sounds, i);
            g_free(sound->payload);
        }
        g_ptr_array_free(package->sounds, TRUE);
    }
    g_free(package->animations); g_free(package->spawns); g_free(package->childs);
g_free(package);
}
