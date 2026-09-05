#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "expression.h"

static EsheepExpressionContext context(void) {
    return (EsheepExpressionContext){
        1920, 1080, 800, 600, 40, 40, 12, 18, 25
    };
}

static void test_arithmetic_and_variables(void) {
    EsheepExpressionContext values = context();
    double result;
    assert(esheep_expression_eval("screenW-imageW-50", &values, &result));
    assert(result == 1830);
    assert(esheep_expression_eval("(areaH/2)-(randS*areaH/2)/120-imageH",
                                  &values, &result));
    assert(fabs(result - 197.5) < 0.001);
    assert(esheep_expression_eval("imageX-imageW*0.9", &values, &result));
    assert(fabs(result + 24) < 0.001);
}

static void test_convert_and_modulo(void) {
    EsheepExpressionContext values = context();
    double result;
    assert(esheep_expression_eval(
        "24+(Convert(screenW/2,System.Int32)%30)/7", &values, &result));
    assert(fabs(result - (24.0 + (960 % 30) / 7.0)) < 0.001);
}

static void test_invalid_expressions_are_rejected(void) {
    EsheepExpressionContext values = context();
    double result;
    assert(!esheep_expression_eval("screenW+", &values, &result));
    assert(!esheep_expression_eval("screenW garbage", &values, &result));
    assert(!esheep_expression_eval("unknown+1", &values, &result));
    assert(!esheep_expression_eval("areaW/0", &values, &result));
    assert(!esheep_expression_valid("1+2 trailing"));
    assert(esheep_expression_valid("-imageH-20"));
}

/* Boundary and overflow tests.  These exercise both the explicit
 * Convert(... System.Int32) narrowing inside the expression language and the
 * defensive esheep_expression_eval_int primitive that every package loader
 * uses for positions, repeats, and child coordinates.  Values at the
 * inclusive edges must succeed; values one ULP beyond must fail.  Negative
 * values inside the representable range must keep succeeding. */
static void test_convert_int32_range(void) {
    EsheepExpressionContext values = context();
    double result;
    /* Inclusive upper edge. */
    assert(esheep_expression_eval("Convert(2147483647,System.Int32)", &values,
                                &result));
    assert(result == 2147483647.0);
    /* One beyond the inclusive upper edge is still safely representable as
     * a double and must be rejected so callers do not observe truncation. */
    assert(!esheep_expression_eval("Convert(2147483648,System.Int32)",
                                   &values, &result));
    /* Inclusive lower edge. */
    assert(esheep_expression_eval("Convert(-2147483648,System.Int32)",
                                  &values, &result));
    assert(result == -2147483648.0);
    /* One below the inclusive lower edge must be rejected. */
    assert(!esheep_expression_eval("Convert(-2147483649,System.Int32)",
                                   &values, &result));
    /* Negative authored positions like the historical walk end of -1 must
     * keep parsing without surfacing an out-of-range failure. */
    assert(esheep_expression_eval("Convert(-1,System.Int32)", &values,
                                  &result));
    assert(result == -1.0);
}

static void test_eval_int_range(void) {
    EsheepExpressionContext values = context();
    int result = -1;
    /* Inclusive edges must succeed for the safe narrowing primitive. */
    assert(esheep_expression_eval_int("2147483647", &values, &result));
    assert(result == 2147483647);
    assert(esheep_expression_eval_int("-2147483648", &values, &result));
    assert(result == -2147483648);
    /* One past either edge must fail without UB. */
    assert(!esheep_expression_eval_int("2147483648", &values, &result));
    assert(result == 0);
    assert(!esheep_expression_eval_int("-2147483649", &values, &result));
    assert(result == 0);
    /* Arithmetic that lands one ULP above the upper edge must also fail.
     * Use a context with a large screen dimension so the multiplication
     * overflows; the default test context (1920x1080) is well within Int32. */
    EsheepExpressionContext huge = { 100000, 100000, 100000, 100000,
                                      40, 40, 0, 0, 0 };
    assert(!esheep_expression_eval_int("screenW*screenW", &huge, &result));
    /* The non-finite path: 1/0 fails parsing already, but an expression
     * that produces +inf via overflow must also be rejected. */
    assert(!esheep_expression_eval_int("screenW*screenW*screenW", &huge,
                                       &result));
    /* Authored negatives remain valid through the safe narrowing. */
    assert(esheep_expression_eval_int("-imageH-20", &values, &result));
    assert(result == -60);
    /* A parse failure must not leave the caller's result undefined. */
    result = 42;
    assert(!esheep_expression_eval_int("unknown+1", &values, &result));
    assert(result == 0);
}

int main(void) {
    test_arithmetic_and_variables();
    test_convert_and_modulo();
    test_invalid_expressions_are_rejected();
    test_convert_int32_range();
    test_eval_int_range();
    puts("All expression tests passed");
    return 0;
}
