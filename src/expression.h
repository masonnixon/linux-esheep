#ifndef ESHEEP_EXPRESSION_H
#define ESHEEP_EXPRESSION_H

#include <glib.h>

typedef struct {
    double screen_width;
    double screen_height;
    double area_width;
    double area_height;
    double image_width;
    double image_height;
    double image_x;
    double image_y;
    double random_value;
} EsheepExpressionContext;

/* Evaluate the small, deliberately bounded arithmetic language used by pet
 * packages. The parser consumes the complete input and rejects unknown names,
 * malformed operators, non-finite values, and division by zero. */
gboolean esheep_expression_eval(const char *expression,
                                 const EsheepExpressionContext *context,
                                 double *result);

/* Validate syntax without depending on a particular screen size. */
gboolean esheep_expression_valid(const char *expression);

#endif
