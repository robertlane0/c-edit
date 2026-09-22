#include <stddef.h>
#include <stdio.h>

#include "edit/ucd.h"

static int checks = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #cond);                                     \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static int expect_lookup(unsigned cp, size_t props, size_t width) {
    size_t p = edit_ucd_lookup(cp);
    ++checks;
    if (p != props || edit_ucd_width(p) != width) {
        fprintf(stderr, "FAIL %d: U+%X props %zu/%zu width %zu/%zu\n", __LINE__, cp, p, props,
                edit_ucd_width(p), width);
        return 1;
    }
    return 0;
}

int main(void) {
    // Vectors from Rust tables (props, capped-by-caller width).
    CHECK(expect_lookup(0x0, 2051, 1) == 0);
    CHECK(expect_lookup(0x7F, 2051, 1) == 0);
    CHECK(expect_lookup(0x80, 2051, 1) == 0);
    CHECK(expect_lookup(0xA0, 2240, 1) == 0);
    CHECK(expect_lookup(0x300, 4, 0) == 0);
    CHECK(expect_lookup(0xD7FF, 2048, 1) == 0);
    CHECK(expect_lookup(0x200D, 15, 0) == 0);
    CHECK(expect_lookup(0xFE0F, 4100, 2) == 0);
    CHECK(expect_lookup(0x1F469, 5582, 2) == 0);
    CHECK(expect_lookup(0x1F3FB, 5572, 2) == 0);
    CHECK(expect_lookup(0x9, 2403, 1) == 0);
    CHECK(expect_lookup(0xA, 2050, 1) == 0);
    CHECK(expect_lookup(0xD, 2049, 1) == 0);
    CHECK(expect_lookup(0x20, 2304, 1) == 0);
    CHECK(expect_lookup(0x3000, 4416, 2) == 0);
    CHECK(expect_lookup(0x10FFFF, 2048, 1) == 0);
    CHECK(expect_lookup(0x2FF0, 5568, 2) == 0);
    // Out of range reads as FFFD (2048/1).
    CHECK(expect_lookup(0x110000, 2048, 1) == 0);
    CHECK(expect_lookup(0xFFFFFFFF, 2048, 1) == 0);

    CHECK(edit_ucd_joins(0, 0x603, 0x603) == 3);
    CHECK(edit_ucd_joins_done(3));
    CHECK(!edit_ucd_joins_done(1));
    CHECK(!edit_ucd_joins_done(0));
    CHECK(edit_ucd_joins(9, 1, 1) == 3); // hardening: bad state stops

    CHECK(!edit_ucd_line_joins(0x603, edit_ucd_lookup(0x300)));
    CHECK(edit_ucd_start_props() == 0x603);
    CHECK(edit_ucd_tab_props() == 0x963);
    CHECK(edit_ucd_lf_props() == 0x802);

    printf("test_ucd: %d checks passed\n", checks);
    return 0;
}
