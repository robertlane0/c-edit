#include <stdio.h>

#include "edit/cell.h"

EDIT_CELL_DEFINE(intcell, int)

typedef struct {
    int x;
    int y;
} point_t;

EDIT_CELL_DEFINE(pointcell, point_t)

static int checks = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #cond);                                     \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

int main(void) {
    intcell_cell_t c = intcell_cell_new(41);
    CHECK(c.value == 41 && c.readers == 0 && !c.writer);

    // Shared borrows stack.
    const int *r1 = intcell_cell_borrow(&c);
    const int *r2 = intcell_cell_borrow(&c);
    CHECK(r1 != NULL && r2 != NULL && *r1 == 41 && *r2 == 41);
    CHECK(c.readers == 2);
    intcell_cell_unborrow(&c);
    CHECK(c.readers == 1);
    intcell_cell_unborrow(&c);
    CHECK(c.readers == 0);

    // Exclusive borrow mutates through the pointer.
    int *w = intcell_cell_borrow_mut(&c);
    CHECK(w != NULL);
    *w = 42;
    CHECK(c.writer);
    intcell_cell_unborrow_mut(&c);
    CHECK(!c.writer);
    CHECK(*intcell_cell_borrow(&c) == 42);
    intcell_cell_unborrow(&c);

    // Works for struct values too.
    point_t p0 = {1, 2};
    pointcell_cell_t pc = pointcell_cell_new(p0);
    point_t *pw = pointcell_cell_borrow_mut(&pc);
    CHECK(pw != NULL);
    pw->x = 9;
    pointcell_cell_unborrow_mut(&pc);
    CHECK(pointcell_cell_borrow(&pc)->x == 9);
    pointcell_cell_unborrow(&pc);

    printf("test_cell: %d checks passed\n", checks);
    return 0;
}
