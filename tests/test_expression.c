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

int main(void) {
    test_arithmetic_and_variables();
    test_convert_and_modulo();
    test_invalid_expressions_are_rejected();
    puts("All expression tests passed");
    return 0;
}
