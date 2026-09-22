#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "edit/doc.h"
#include "edit/nav.h"

static int checks = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #cond);                                     \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static int expect_all(const char *text, const size_t *fwd, const size_t *bwd,
                      const size_t (*sel)[2]) {
    size_t n = strlen(text);
    edit_slice_doc_t s;
    edit_slice_doc_init(&s, (const uint8_t *)text, n);
    for (size_t off = 0; off <= n; ++off) {
        size_t f = edit_word_forward(&s.doc, off);
        size_t b = edit_word_backward(&s.doc, off);
        size_t sb = 0;
        size_t se = 0;
        edit_word_select(&s.doc, off, &sb, &se);
        ++checks;
        if (f != fwd[off] || b != bwd[off] || sb != sel[off][0] || se != sel[off][1]) {
            fprintf(stderr, "FAIL %d: %s off %zu fwd %zu/%zu bwd %zu/%zu sel %zu,%zu/%zu,%zu\n",
                    __LINE__, text, off, f, fwd[off], b, bwd[off], sb, se, sel[off][0],
                    sel[off][1]);
            return 1;
        }
    }
    // Out-of-bounds offsets clamp.
    CHECK(edit_word_forward(&s.doc, n + 99) == fwd[n]);
    CHECK(edit_word_backward(&s.doc, n + 99) == bwd[n]);
    size_t sb = 0;
    size_t se = 0;
    edit_word_select(&s.doc, n + 99, &sb, &se);
    CHECK(sb == sel[n][0] && se == sel[n][1]);
    return 0;
}

int main(void) {
    // Port of Rust test_word_navigation.
    {
        edit_slice_doc_t s;
        edit_slice_doc_init(&s, (const uint8_t *)"Hello World", 11);
        CHECK(edit_word_forward(&s.doc, 0) == 5);
        edit_slice_doc_init(&s, (const uint8_t *)"Hello,World", 11);
        CHECK(edit_word_forward(&s.doc, 0) == 5);
        edit_slice_doc_init(&s, (const uint8_t *)"   Hello", 8);
        CHECK(edit_word_forward(&s.doc, 0) == 8);
        edit_slice_doc_init(&s, (const uint8_t *)"\n\nHello", 7);
        CHECK(edit_word_forward(&s.doc, 0) == 1);
        edit_slice_doc_init(&s, (const uint8_t *)"Hello World", 11);
        CHECK(edit_word_backward(&s.doc, 11) == 6);
        edit_slice_doc_init(&s, (const uint8_t *)"Hello,World", 11);
        CHECK(edit_word_backward(&s.doc, 10) == 6);
        edit_slice_doc_init(&s, (const uint8_t *)"Hello   ", 8);
        CHECK(edit_word_backward(&s.doc, 7) == 0);
        edit_slice_doc_init(&s, (const uint8_t *)"Hello\n\n", 7);
        CHECK(edit_word_backward(&s.doc, 7) == 6);
    }

    // Differential tables from Rust navigation.rs.
    static const size_t hw_f[] = {5, 5, 5, 5, 5, 11, 11, 11, 11, 11, 11, 11};
    static const size_t hw_b[] = {0, 0, 0, 0, 0, 0, 0, 6, 6, 6, 6, 6};
    static const size_t hw_s[][2] = {{0, 5},  {0, 5},  {0, 5},  {0, 5},  {0, 5},  {5, 6},
                                     {6, 11}, {6, 11}, {6, 11}, {6, 11}, {6, 11}, {6, 11}};
    CHECK(expect_all("Hello World", hw_f, hw_b, hw_s) == 0);
    CHECK(expect_all("Hello,World", hw_f, hw_b, hw_s) == 0);

    static const size_t sp_f[] = {8, 8, 8, 8, 8, 8, 8, 8, 8};
    static const size_t sp_b[] = {0, 0, 0, 0, 3, 3, 3, 3, 3};
    static const size_t sp_s[][2] = {{0, 3}, {0, 3}, {0, 3}, {3, 8}, {3, 8},
                                     {3, 8}, {3, 8}, {3, 8}, {3, 8}};
    CHECK(expect_all("   Hello", sp_f, sp_b, sp_s) == 0);

    static const size_t nl_f[] = {1, 7, 7, 7, 7, 7, 7, 7};
    static const size_t nl_b[] = {0, 0, 1, 2, 2, 2, 2, 2};
    static const size_t nl_s[][2] = {{0, 0}, {1, 1}, {2, 7}, {2, 7},
                                     {2, 7}, {2, 7}, {2, 7}, {2, 7}};
    CHECK(expect_all("\n\nHello", nl_f, nl_b, nl_s) == 0);

    static const size_t cx_f[] = {3,  3,  3,  7,  7,  7,  7,  9,  9, 14,
                                  14, 14, 14, 14, 18, 18, 18, 18, 18};
    static const size_t cx_b[] = {0, 0, 0, 0, 0, 4, 4, 4, 4, 7, 7, 7, 11, 11, 11, 11, 15, 15, 15};
    static const size_t cx_s[][2] = {{0, 3},   {0, 3},   {0, 3},   {3, 4},   {4, 7},
                                     {4, 7},   {4, 7},   {7, 9},   {7, 9},   {9, 11},
                                     {9, 11},  {11, 14}, {11, 14}, {11, 14}, {11, 14},
                                     {15, 18}, {15, 18}, {15, 18}, {15, 18}};
    CHECK(expect_all("foo(bar);  baz\nqux", cx_f, cx_b, cx_s) == 0);

    static const size_t e_f[] = {0};
    static const size_t e_b[] = {0};
    static const size_t e_s[][2] = {{0, 0}};
    CHECK(expect_all("", e_f, e_b, e_s) == 0);

    static const size_t ws_f[] = {3, 3, 3, 3};
    static const size_t ws_b[] = {0, 0, 0, 0};
    static const size_t ws_s[][2] = {{0, 3}, {0, 3}, {0, 3}, {0, 3}};
    CHECK(expect_all("   ", ws_f, ws_b, ws_s) == 0);

    static const size_t se_f[] = {1, 3, 3, 5, 5, 7, 7, 7};
    static const size_t se_b[] = {0, 0, 0, 2, 2, 4, 4, 6};
    static const size_t se_s[][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 4},
                                     {4, 5}, {5, 6}, {6, 7}, {6, 7}};
    CHECK(expect_all("a,b;c d", se_f, se_b, se_s) == 0);

    static const size_t ml_f[] = {6, 6, 6, 6, 6, 6, 12, 12, 12, 12, 12, 12, 13, 13};
    static const size_t ml_b[] = {0, 0, 1, 1, 1, 1, 1, 1, 7, 7, 7, 7, 7, 7};
    static const size_t ml_s[][2] = {{0, 1},  {1, 6},  {1, 6},  {1, 6},  {1, 6},
                                     {1, 6},  {1, 6},  {7, 12}, {7, 12}, {7, 12},
                                     {7, 12}, {7, 12}, {7, 12}, {13, 13}};
    CHECK(expect_all(" line1\nline2\n", ml_f, ml_b, ml_s) == 0);

    // NULL docs are safe no-ops.
    CHECK(edit_word_forward(NULL, 5) == 0);
    CHECK(edit_word_backward(NULL, 5) == 0);
    size_t b = 9;
    size_t e = 9;
    edit_word_select(NULL, 5, &b, &e);
    CHECK(b == 0 && e == 0);
    CHECK(edit_doc_len(NULL) == 0);

    printf("test_nav: %d checks passed\n", checks);
    return 0;
}
