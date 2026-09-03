#include "expression.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *cursor;
    const EsheepExpressionContext *context;
    gboolean failed;
} Parser;

static void skip_space(Parser *parser) {
    while (isspace((unsigned char)*parser->cursor)) parser->cursor++;
}

static gboolean take(Parser *parser, char wanted) {
    skip_space(parser);
    if (*parser->cursor != wanted) return FALSE;
    parser->cursor++;
    return TRUE;
}

static double variable_value(Parser *parser, const char *name, size_t length) {
#define VARIABLE(name_literal, field) \
    if (length == sizeof(name_literal) - 1 && \
        strncmp(name, name_literal, length) == 0) return parser->context->field
    VARIABLE("screenW", screen_width);
    VARIABLE("screenH", screen_height);
    VARIABLE("areaW", area_width);
    VARIABLE("areaH", area_height);
    VARIABLE("imageW", image_width);
    VARIABLE("imageH", image_height);
    VARIABLE("imageX", image_x);
    VARIABLE("imageY", image_y);
    VARIABLE("randS", random_value);
    VARIABLE("random", random_value);
#undef VARIABLE
    parser->failed = TRUE;
    return 0.0;
}

static double parse_additive(Parser *parser);

static double parse_primary(Parser *parser) {
    skip_space(parser);
    if (take(parser, '(')) {
        double value = parse_additive(parser);
        if (!take(parser, ')')) parser->failed = TRUE;
        return value;
    }

    const char *start = parser->cursor;
    if (isdigit((unsigned char)*start) || *start == '.') {
        char *end = NULL;
        errno = 0;
        double value = g_ascii_strtod(start, &end);
        if (end == start || errno || !isfinite(value)) {
            parser->failed = TRUE;
            return 0.0;
        }
        parser->cursor = end;
        return value;
    }

    if (isalpha((unsigned char)*start) || *start == '_') {
        const char *name = start;
        while (isalnum((unsigned char)*parser->cursor) ||
               *parser->cursor == '_') parser->cursor++;
        size_t length = (size_t)(parser->cursor - name);
        if (length == 7 && strncmp(name, "Convert", length) == 0) {
            if (!take(parser, '(')) {
                parser->failed = TRUE;
                return 0.0;
            }
            double value = parse_additive(parser);
            if (!take(parser, ',')) parser->failed = TRUE;
            skip_space(parser);
            const char *type = parser->cursor;
            while (isalnum((unsigned char)*parser->cursor) ||
                   *parser->cursor == '.') parser->cursor++;
            if ((size_t)(parser->cursor - type) != 12 ||
                strncmp(type, "System.Int32", 12) != 0)
                parser->failed = TRUE;
            if (!take(parser, ')')) parser->failed = TRUE;
            return (double)(int)value;
        }
        return variable_value(parser, name, length);
    }

    parser->failed = TRUE;
    return 0.0;
}

static double parse_unary(Parser *parser) {
    skip_space(parser);
    if (take(parser, '+')) return parse_unary(parser);
    if (take(parser, '-')) return -parse_unary(parser);
    return parse_primary(parser);
}

static double parse_multiplicative(Parser *parser) {
    double value = parse_unary(parser);
    for (;;) {
        skip_space(parser);
        char operator = *parser->cursor;
        if (operator != '*' && operator != '/' && operator != '%') return value;
        parser->cursor++;
        double rhs = parse_unary(parser);
        if ((operator == '/' || operator == '%') && rhs == 0.0) {
            parser->failed = TRUE;
            return 0.0;
        }
        if (operator == '*') value *= rhs;
        else if (operator == '/') value /= rhs;
        else value = fmod(value, rhs);
    }
}

static double parse_additive(Parser *parser) {
    double value = parse_multiplicative(parser);
    for (;;) {
        skip_space(parser);
        char operator = *parser->cursor;
        if (operator != '+' && operator != '-') return value;
        parser->cursor++;
        double rhs = parse_multiplicative(parser);
        if (operator == '+') value += rhs;
        else value -= rhs;
    }
}

gboolean esheep_expression_eval(const char *expression,
                                const EsheepExpressionContext *context,
                                double *result) {
    if (!expression || !context || !result) return FALSE;
    Parser parser = { expression, context, FALSE };
    double value = parse_additive(&parser);
    skip_space(&parser);
    if (parser.failed || *parser.cursor != '\0' || !isfinite(value)) return FALSE;
    *result = value;
    return TRUE;
}

gboolean esheep_expression_valid(const char *expression) {
    EsheepExpressionContext context = { 1, 1, 1, 1, 1, 1, 1, 1, 1 };
    double result;
    return esheep_expression_eval(expression, &context, &result);
}
