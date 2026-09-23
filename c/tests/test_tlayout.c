#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "edit/arena.h"
#include "edit/helpers.h"
#include "edit/tree.h"

static int checks = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #cond);                                     \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static int expect_rect(edit_rect_t got, int l, int t, int r, int b) {
    ++checks;
    if (got.left != l || got.top != t || got.right != r || got.bottom != b) {
        fprintf(stderr, "FAIL %d: rect (%d,%d,%d,%d) != (%d,%d,%d,%d)\n", __LINE__, got.left,
                got.top, got.right, got.bottom, l, t, r, b);
        return 1;
    }
    return 0;
}

static edit_tnode_t *add(edit_tree_t *t, const char *name, int w, int h) {
    edit_tnode_t *n = edit_tree_block_begin(t, name);
    if (n == NULL) {
        return NULL;
    }
    n->intrinsic_size.width = w;
    n->intrinsic_size.height = h;
    n->intrinsic_set = true;
    edit_tree_block_end(t);
    return n;
}

int main(void) {
    edit_arena_t arena;
    memset(&arena, 0, sizeof arena);
    CHECK(edit_arena_init(&arena, 65536) == 0);

    // Vertical stack: header (padded) + stretch body + right-aligned chip.
    edit_tree_t t;
    edit_tree_init(&t, &arena);
    edit_tnode_t *header = edit_tree_block_begin(&t, "header");
    CHECK(header != NULL);
    header->intrinsic_size.width = 10;
    header->intrinsic_size.height = 2;
    header->intrinsic_set = true;
    header->attributes.padding.left = 1;
    header->attributes.padding.right = 1;
    edit_tree_block_end(&t);
    edit_tnode_t *body = edit_tree_block_begin(&t, "body");
    CHECK(body != NULL);
    body->intrinsic_size.width = 0;
    body->intrinsic_size.height = 3;
    body->intrinsic_set = true;
    edit_tree_block_end(&t);
    edit_tnode_t *chip = edit_tree_block_begin(&t, "chip");
    CHECK(chip != NULL);
    chip->intrinsic_size.width = 4;
    chip->intrinsic_size.height = 1;
    chip->intrinsic_set = true;
    chip->attributes.position = EDIT_POS_RIGHT;
    edit_tree_block_end(&t);

    edit_rect_t viewport = {0, 0, 20, 10};
    edit_tree_layout(&t, viewport);
    // Root fills the viewport.
    CHECK(expect_rect(t.root_first->outer, 0, 0, 20, 10) == 0);
    // header: outer 20x2 at y=0 (stretch), inner shrunk by padding.
    CHECK(expect_rect(header->outer, 0, 0, 20, 2) == 0);
    CHECK(expect_rect(header->inner, 1, 0, 19, 2) == 0);
    // body: full width, y=2..5.
    CHECK(expect_rect(body->outer, 0, 2, 20, 5) == 0);
    // chip: right-aligned 4-wide at y=5.
    CHECK(expect_rect(chip->outer, 16, 5, 20, 6) == 0);
    CHECK(expect_rect(chip->inner, 16, 5, 20, 6) == 0);

    // Bordered box (left-aligned) shrinks inner by 1 on each side.
    edit_tree_t t2;
    edit_tree_init(&t2, &arena);
    edit_tnode_t *box = edit_tree_block_begin(&t2, "box");
    CHECK(box != NULL);
    box->attributes.bordered = true;
    box->attributes.position = EDIT_POS_LEFT;
    box->intrinsic_size.width = 8;
    box->intrinsic_size.height = 4;
    box->intrinsic_set = true;
    edit_tree_block_end(&t2);
    edit_tree_layout(&t2, viewport);
    CHECK(expect_rect(box->outer, 0, 0, 10, 6) == 0); // intrinsic 8+2 x 4+2
    CHECK(expect_rect(box->inner, 1, 1, 9, 5) == 0);

    // Table: columns size to widest cell, gap applied.
    edit_tree_t t3;
    edit_tree_init(&t3, &arena);
    edit_tnode_t *tab = edit_tree_block_begin(&t3, "tab");
    CHECK(tab != NULL);
    edit_tree_make_table(tab);
    tab->table_gap.width = 1;
    tab->table_gap.height = 1;
    edit_tnode_t *row = edit_tree_block_begin(&t3, "row");
    CHECK(row != NULL);
    edit_tnode_t *c0 = add(&t3, "c0", 3, 1);
    edit_tnode_t *c1 = add(&t3, "c1", 5, 2);
    CHECK(c0 != NULL && c1 != NULL);
    edit_tree_block_end(&t3); // row
    edit_tnode_t *row2 = edit_tree_block_begin(&t3, "row2");
    CHECK(row2 != NULL);
    edit_tnode_t *c2 = add(&t3, "c2", 4, 1);
    CHECK(c2 != NULL);
    edit_tree_block_end(&t3); // row2
    edit_tree_block_end(&t3); // tab
    edit_tree_layout(&t3, (edit_rect_t){0, 0, 20, 10});
    // Columns: [max(3,4), 5] = [4,5]; inner width 4+1+5=10, rows full width.
    CHECK(expect_rect(row->outer, 0, 0, 20, 2) == 0);
    CHECK(expect_rect(c0->outer, 0, 0, 4, 1) == 0);
    CHECK(expect_rect(c1->outer, 5, 0, 10, 2) == 0);
    CHECK(expect_rect(row2->outer, 0, 3, 20, 4) == 0);
    CHECK(expect_rect(c2->outer, 0, 3, 4, 4) == 0);

    // Scrollarea clamps the offset and shifts content up.
    edit_tree_t t4;
    edit_tree_init(&t4, &arena);
    edit_tnode_t *scroll = edit_tree_block_begin(&t4, "scroll");
    CHECK(scroll != NULL);
    edit_tree_make_scrollarea(scroll);
    scroll->intrinsic_size.width = 10;
    scroll->intrinsic_size.height = 2;
    scroll->intrinsic_set = true;
    edit_tnode_t *content = edit_tree_block_begin(&t4, "content");
    CHECK(content != NULL);
    content->intrinsic_size.width = 10;
    content->intrinsic_size.height = 8;
    content->intrinsic_set = true;
    edit_tree_block_end(&t4); // content
    edit_tree_block_end(&t4); // scroll
    // Scroll down 3 (max 8-2=6, so 3 sticks).
    {
        edit_tnode_t *sc = scroll;
        sc->scroll_offset.y = 3;
    }
    edit_tree_layout(&t4, (edit_rect_t){0, 0, 20, 10});
    CHECK(expect_rect(scroll->outer, 0, 0, 20, 2) == 0);
    CHECK(expect_rect(content->outer, 0, -3, 19, 5) == 0);

    // Floater anchors to the viewport with gravity.
    edit_tree_t t5;
    edit_tree_init(&t5, &arena);
    edit_tnode_t *fl = edit_tree_block_begin(&t5, "float");
    CHECK(fl != NULL);
    fl->intrinsic_size.width = 6;
    fl->intrinsic_size.height = 2;
    fl->intrinsic_set = true;
    fl->attributes.float_attr.has_float = true;
    fl->attributes.float_attr.gravity_x = 0.5f;
    fl->attributes.float_attr.gravity_y = 0.5f;
    fl->attributes.float_attr.offset_x = 10.0f;
    fl->attributes.float_attr.offset_y = 5.0f;
    edit_tree_block_end(&t5);
    // Float placement only applies to roots.
    edit_tree_to_root(&t5, fl, NULL);
    edit_tree_layout(&t5, (edit_rect_t){0, 0, 20, 10});
    // x = 10 - 0.5*6 = 7, y = 5 - 0.5*2 = 4.
    CHECK(expect_rect(fl->outer, 7, 4, 13, 6) == 0);

    // NULL safety.
    edit_tree_make_table(NULL);
    edit_tree_make_scrollarea(NULL);
    edit_tree_layout(NULL, viewport);

    edit_tree_destroy(&t);
    edit_tree_destroy(&t2);
    edit_tree_destroy(&t3);
    edit_tree_destroy(&t4);
    edit_tree_destroy(&t5);
    edit_arena_destroy(&arena);
    printf("test_tlayout: %d checks passed\n", checks);
    return 0;
}
