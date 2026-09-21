#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "edit/apperr.h"
#include "edit/helpers.h"

static int checks = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #cond);                                     \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static int expect_metric(size_t value, const char *want) {
    char buf[32];
    int n = edit_metric_format(buf, sizeof buf, value);
    ++checks;
    if (n != (int)strlen(want)) {
        fprintf(stderr, "FAIL %d: metric %zu len %d != %zu\n", __LINE__, value, n, strlen(want));
        return 1;
    }
    ++checks;
    if (strcmp(buf, want) != 0) {
        fprintf(stderr, "FAIL %d: metric %zu = %s\n", __LINE__, value, buf);
        return 1;
    }
    return 0;
}

int main(void) {
    // apperr kinds and codes.
    CHECK(edit_error_equal(edit_error_app(0), EDIT_APP_ICU_MISSING));
    CHECK(!edit_error_equal(edit_error_app(0), edit_error_app(1)));
    CHECK(!edit_error_equal(edit_error_app(0), edit_error_icu(0)));
    CHECK(!edit_error_equal(edit_error_icu(0), edit_error_sys(0)));
    CHECK(edit_error_equal(edit_error_sys(7), edit_error_sys(7)));
    CHECK(EDIT_APP_ICU_MISSING.kind == EDIT_ERR_APP && EDIT_APP_ICU_MISSING.code == 0);

    // Metric formatting mirrors helpers::MetricFormatter.
    CHECK(expect_metric(0, "0B") == 0);
    CHECK(expect_metric(1, "1B") == 0);
    CHECK(expect_metric(999, "999B") == 0);
    CHECK(expect_metric(1000, "1kB") == 0);
    CHECK(expect_metric(1500, "1kB") == 0);
    CHECK(expect_metric(999999, "999kB") == 0);
    CHECK(expect_metric(1000000, "1MB") == 0);
    CHECK(expect_metric(2500000, "2MB") == 0);
    CHECK(expect_metric(1000000000, "1GB") == 0);
    CHECK(expect_metric(5500000000, "5GB") == 0);

    // Truncation stays NUL-terminated; length query works.
    char small[4];
    int full = edit_metric_format(small, sizeof small, 1500);
    CHECK(full == 3);
    CHECK(small[sizeof small - 1] == '\0');
    int qlen = edit_metric_format(NULL, 0, 1500);
    CHECK(qlen == 3);

    // Point order is row-major (y, then x).
    edit_point_t a = {1, 2};
    edit_point_t b = {9, 2};
    edit_point_t c = {0, 3};
    CHECK(edit_point_cmp(a, b) < 0);
    CHECK(edit_point_cmp(b, a) > 0);
    CHECK(edit_point_cmp(a, a) == 0);
    CHECK(edit_point_cmp(b, c) < 0);
    CHECK(edit_point_eq(a, a));
    CHECK(!edit_point_eq(a, b));
    CHECK(edit_point_eq(EDIT_POINT_MIN, EDIT_POINT_MIN));
    CHECK(edit_point_eq(EDIT_POINT_MAX, EDIT_POINT_MAX));

    // Rect constructors mimic CSS padding shorthands.
    edit_rect_t r1 = edit_rect_one(3);
    CHECK(r1.left == 3 && r1.top == 3 && r1.right == 3 && r1.bottom == 3);
    edit_rect_t r2 = edit_rect_two(1, 2);
    CHECK(r2.left == 2 && r2.top == 1 && r2.right == 2 && r2.bottom == 1);
    edit_rect_t r3 = edit_rect_three(1, 2, 3);
    CHECK(r3.left == 2 && r3.top == 1 && r3.right == 2 && r3.bottom == 3);
    edit_size_t s = {10, 20};
    edit_rect_t rs = edit_size_as_rect(s);
    CHECK(rs.left == 0 && rs.top == 0 && rs.right == 10 && rs.bottom == 20);

    // Empty, size, contains.
    CHECK(edit_rect_is_empty(r1));
    CHECK(!edit_rect_is_empty(rs));
    edit_rect_t empty = {5, 5, 5, 9};
    CHECK(edit_rect_is_empty(empty));
    CHECK(edit_rect_width(rs) == 10);
    CHECK(edit_rect_height(rs) == 20);
    CHECK(edit_rect_width(empty) == 0);
    edit_point_t inside = {5, 5};
    edit_point_t edge = {10, 5};
    edit_point_t origin = {0, 0};
    edit_point_t bottom_edge = {5, 20};
    CHECK(edit_rect_contains(rs, inside));
    CHECK(edit_rect_contains(rs, origin));
    CHECK(!edit_rect_contains(rs, edge)); // right/bottom exclusive
    CHECK(!edit_rect_contains(rs, bottom_edge));

    // Intersect incl. disjoint clamp.
    edit_rect_t x = {0, 0, 10, 10};
    edit_rect_t y = {5, 5, 15, 15};
    edit_rect_t i = edit_rect_intersect(x, y);
    CHECK(i.left == 5 && i.top == 5 && i.right == 10 && i.bottom == 10);
    edit_rect_t z = {20, 20, 30, 30};
    edit_rect_t d = edit_rect_intersect(x, z);
    CHECK(edit_rect_is_empty(d));
    CHECK(edit_rect_width(d) >= 0 && edit_rect_height(d) >= 0);

    // Saturating width on extreme coords.
    edit_rect_t huge = {INT32_MIN, 0, INT32_MAX, 1};
    CHECK(edit_rect_width(huge) == INT32_MAX);

    // minmax orders pairs.
    int32_t out[2];
    edit_minmax_i32(2, 1, out);
    CHECK(out[0] == 1 && out[1] == 2);
    edit_minmax_i32(1, 2, out);
    CHECK(out[0] == 1 && out[1] == 2);
    edit_minmax_i32(5, 5, out);
    CHECK(out[0] == 5 && out[1] == 5);
    edit_minmax_i32(1, 2, NULL); // no-op, must not crash

    printf("test_helpers: %d checks passed\n", checks);
    return 0;
}
