#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "edit/doc.h"
#include "edit/helpers.h"
#include "edit/measure.h"

static int checks = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #cond);                                     \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

// Chunked doc (cf. Rust ChunkedDoc): splits never break graphemes here.
typedef struct {
    edit_doc_t doc;
    const uint8_t **chunks;
    const size_t *lens;
    size_t nchunks;
    size_t total;
} chunked_t;

static size_t chunked_len(const void *ctx) {
    const chunked_t *c = (const chunked_t *)ctx;
    return c == NULL ? 0 : c->total;
}

static void chunked_fwd(const void *ctx, size_t off, const uint8_t **p, size_t *n) {
    const chunked_t *c = (const chunked_t *)ctx;
    if (c == NULL || p == NULL || n == NULL) {
        return;
    }
    *p = NULL;
    *n = 0;
    for (size_t i = 0; i < c->nchunks; ++i) {
        if (off < c->lens[i]) {
            *p = c->chunks[i] + off;
            *n = c->lens[i] - off;
            return;
        }
        off -= c->lens[i];
    }
}

static void chunked_bwd(const void *ctx, size_t off, const uint8_t **p, size_t *n) {
    const chunked_t *c = (const chunked_t *)ctx;
    if (c == NULL || p == NULL || n == NULL) {
        return;
    }
    *p = NULL;
    *n = 0;
    for (size_t k = 0; k < c->nchunks; ++k) {
        size_t i = c->nchunks - 1 - k;
        if (off < c->lens[i]) {
            *p = c->chunks[i];
            *n = c->lens[i] - off;
            return;
        }
        off -= c->lens[i];
    }
}

static void chunked_init(chunked_t *c, const uint8_t **chunks, const size_t *lens, size_t n) {
    c->chunks = chunks;
    c->lens = lens;
    c->nchunks = n;
    c->total = 0;
    for (size_t i = 0; i < n; ++i) {
        c->total += lens[i];
    }
    c->doc.ctx = c;
    c->doc.len = chunked_len;
    c->doc.read_fwd = chunked_fwd;
    c->doc.read_bwd = chunked_bwd;
    c->doc.replace = NULL;
}

static int expect_cursor(edit_cursor_t got, size_t off, int lx, int ly, int vx, int vy, int col,
                         bool wopp) {
    ++checks;
    if (got.offset != off || got.logical.x != lx || got.logical.y != ly || got.visual.x != vx ||
        got.visual.y != vy || got.column != col || got.wrap_opp != wopp) {
        fprintf(stderr,
                "FAIL %d: got off=%zu log=(%d,%d) vis=(%d,%d) col=%d w=%d want off=%zu (%d,%d) "
                "(%d,%d) %d %d\n",
                __LINE__, got.offset, got.logical.x, got.logical.y, got.visual.x, got.visual.y,
                got.column, (int)got.wrap_opp, off, lx, ly, vx, vy, col, (int)wopp);
        return 1;
    }
    return 0;
}

static edit_point_t pt(int x, int y) {
    edit_point_t p = {x, y};
    return p;
}

int main(void) {
    // test_measure_forward_newline_start
    {
        edit_slice_doc_t d;
        edit_slice_doc_init(&d, (const uint8_t *)"foo\nbar", 7);
        edit_measure_t m;
        edit_measure_init(&m, &d.doc);
        CHECK(expect_cursor(edit_measure_goto_visual(&m, pt(0, 1)), 4, 0, 1, 0, 1, 0, false) == 0);
    }
    // test_measure_forward_clipped_wide_char: a😶‍🌫️b, target x=2
    {
        static const uint8_t t[] = "a\xF0\x9F\x98\xB6\xE2\x80\x8D\xF0\x9F\x8C\xAB\xEF\xB8\x8F"
                                   "b";
        edit_slice_doc_t d;
        edit_slice_doc_init(&d, t, sizeof t - 1);
        edit_measure_t m;
        edit_measure_init(&m, &d.doc);
        CHECK(expect_cursor(edit_measure_goto_visual(&m, pt(2, 0)), 1, 1, 0, 1, 0, 1, false) == 0);
    }
    // test_measure_forward_word_wrap: "foo bar \nbaz", wrap 6
    {
        edit_slice_doc_t d;
        edit_slice_doc_init(&d, (const uint8_t *)"foo bar \nbaz", 12);
        edit_measure_t m;
        edit_measure_init(&m, &d.doc);
        edit_measure_set_wrap(&m, 6);
        CHECK(expect_cursor(edit_measure_goto_logical(&m, pt(5, 0)), 5, 5, 0, 1, 1, 5, true) == 0);
    }
    {
        edit_slice_doc_t d;
        edit_slice_doc_init(&d, (const uint8_t *)"foo bar \nbaz", 12);
        edit_measure_t m;
        edit_measure_init(&m, &d.doc);
        edit_measure_set_wrap(&m, 6);
        CHECK(expect_cursor(edit_measure_goto_visual(&m, pt(INT32_MAX, 0)), 4, 4, 0, 4, 0, 4,
                            true) == 0);
        edit_cursor_t c0 = {1, {1, 0}, {1, 0}, 1, false};
        edit_measure_set_cursor(&m, c0);
        CHECK(expect_cursor(edit_measure_goto_visual(&m, pt(5, 0)), 4, 4, 0, 4, 0, 4, true) == 0);
        CHECK(expect_cursor(edit_measure_goto_visual(&m, pt(0, 1)), 4, 4, 0, 0, 1, 4, false) == 0);
        CHECK(expect_cursor(edit_measure_goto_visual(&m, pt(5, 1)), 8, 8, 0, 4, 1, 8, false) == 0);
        CHECK(expect_cursor(edit_measure_goto_visual(&m, pt(0, 2)), 9, 0, 1, 0, 2, 0, false) == 0);
        CHECK(expect_cursor(edit_measure_goto_visual(&m, pt(5, 2)), 12, 3, 1, 3, 2, 3, false) == 0);
    }
    // test_measure_forward_tabs: "a\tb\tc", tab 4, visual x=4
    {
        edit_slice_doc_t d;
        edit_slice_doc_init(&d, (const uint8_t *)"a\tb\tc", 5);
        edit_measure_t m;
        edit_measure_init(&m, &d.doc);
        edit_measure_set_tab(&m, 4);
        CHECK(expect_cursor(edit_measure_goto_visual(&m, pt(4, 0)), 2, 2, 0, 4, 0, 4, false) == 0);
    }
    // test_measure_forward_chunk_boundaries
    {
        static const uint8_t c0[] = "Hello";
        static const uint8_t c1[] = "\xF0\x9F\x91\xA9\xF0\x9F\x8F\xBB"; // 👩🏻 8B/2col
        static const uint8_t c2[] = "World";
        const uint8_t *chunks[] = {c0, c1, c2};
        const size_t lens[] = {5, 8, 5};
        chunked_t cd;
        chunked_init(&cd, chunks, lens, 3);
        edit_measure_t m;
        edit_measure_init(&m, &cd.doc);
        edit_cursor_t c = edit_measure_goto_visual(&m, pt(5 + 2 + 3, 0));
        CHECK(c.offset == 5 + 8 + 3);
        CHECK(c.logical.x == 5 + 1 + 3 && c.logical.y == 0);
    }
    // test_exact_wrap: "foo bar.\nabc" chunked, wrap 7
    {
        static const uint8_t c0[] = "foo ";
        static const uint8_t c1[] = "bar";
        static const uint8_t c2[] = ".\n";
        static const uint8_t c3[] = "abc";
        const uint8_t *chunks[] = {c0, c1, c2, c3};
        const size_t lens[] = {4, 3, 2, 3};
        chunked_t cd;
        chunked_init(&cd, chunks, lens, 4);
        edit_measure_t m;
        edit_measure_init(&m, &cd.doc);
        edit_measure_set_wrap(&m, 7);
        CHECK(expect_cursor(edit_measure_goto_visual(&m, pt(7, 0)), 4, 4, 0, 4, 0, 4, true) == 0);
        CHECK(expect_cursor(edit_measure_goto_visual(&m, pt(0, 1)), 4, 4, 0, 0, 1, 4, false) == 0);
        CHECK(expect_cursor(edit_measure_goto_visual(&m, pt(INT32_MAX, 1)), 8, 8, 0, 4, 1, 8,
                            false) == 0);
        CHECK(expect_cursor(edit_measure_goto_visual(&m, pt(0, 2)), 9, 0, 1, 0, 2, 0, false) == 0);
        CHECK(expect_cursor(edit_measure_goto_visual(&m, pt(INT32_MAX, 2)), 12, 3, 1, 3, 2, 3,
                            false) == 0);
    }
    // test_force_wrap: "// aaaaaaaaaaaa", wrap 8
    {
        edit_slice_doc_t d;
        edit_slice_doc_init(&d, (const uint8_t *)"// aaaaaaaaaaaa", 15);
        edit_measure_t m;
        edit_measure_init(&m, &d.doc);
        edit_measure_set_wrap(&m, 8);
        CHECK(expect_cursor(edit_measure_goto_visual(&m, pt(INT32_MAX, 0)), 3, 3, 0, 3, 0, 3,
                            true) == 0);
        CHECK(expect_cursor(edit_measure_goto_visual(&m, pt(0, 1)), 3, 3, 0, 0, 1, 3, false) == 0);
        CHECK(expect_cursor(edit_measure_goto_logical(&m, pt(4, 0)), 4, 4, 0, 1, 1, 4, false) == 0);
        CHECK(expect_cursor(edit_measure_goto_visual(&m, pt(INT32_MAX, 1)), 11, 11, 0, 8, 1, 11,
                            true) == 0);
        CHECK(expect_cursor(edit_measure_goto_visual(&m, pt(INT32_MAX, 2)), 15, 15, 0, 4, 2, 15,
                            false) == 0);
    }
    // test_force_wrap_wide: hexagrams, wrap 5
    {
        static const uint8_t t[] = "䷀䷁䷂䷃䷄䷅䷆䷇䷈䷉";
        static const char *exp[] = {"䷀䷁", "䷂䷃", "䷄䷅", "䷆䷇", "䷈䷉"};
        edit_slice_doc_t d;
        edit_slice_doc_init(&d, t, sizeof t - 1);
        edit_measure_t m;
        edit_measure_init(&m, &d.doc);
        edit_measure_set_wrap(&m, 5);
        for (int y = 0; y < 5; ++y) {
            edit_cursor_t b = edit_measure_goto_visual(&m, pt(0, y));
            edit_cursor_t e = edit_measure_goto_visual(&m, pt(5, y));
            size_t n = e.offset - b.offset;
            CHECK(n == strlen(exp[y]));
            CHECK(memcmp(t + b.offset, exp[y], n) == 0);
        }
    }
    // test_force_wrap_column
    {
        edit_slice_doc_t d;
        edit_slice_doc_init(&d, (const uint8_t *)"// aaaaaaaaaaaa", 15);
        edit_measure_t m;
        edit_measure_init(&m, &d.doc);
        edit_measure_set_wrap(&m, 8);
        edit_cursor_t end0 = edit_measure_goto_visual(&m, pt(INT32_MAX, 0));
        CHECK(expect_cursor(end0, 3, 3, 0, 3, 0, 3, true) == 0);
        CHECK(expect_cursor(edit_measure_goto_visual(&m, pt(end0.visual.x, 1)), 6, 6, 0, 3, 1, 6,
                            false) == 0);
        CHECK(expect_cursor(edit_measure_goto_visual(&m, pt(end0.visual.x, 2)), 14, 14, 0, 3, 2, 14,
                            false) == 0);
    }
    // test_any_wrap: "// ------------", wrap 8
    {
        edit_slice_doc_t d;
        edit_slice_doc_init(&d, (const uint8_t *)"// ------------", 15);
        edit_measure_t m;
        edit_measure_init(&m, &d.doc);
        edit_measure_set_wrap(&m, 8);
        CHECK(expect_cursor(edit_measure_goto_visual(&m, pt(INT32_MAX, 0)), 8, 8, 0, 8, 0, 8,
                            true) == 0);
        CHECK(expect_cursor(edit_measure_goto_visual(&m, pt(INT32_MAX, 1)), 15, 15, 0, 7, 1, 15,
                            true) == 0);
    }
    // test_any_wrap_wide
    {
        static const uint8_t t[] = "零一二三四五六七八九";
        static const char *exp[] = {"零一", "二三", "四五", "六七", "八九"};
        edit_slice_doc_t d;
        edit_slice_doc_init(&d, t, sizeof t - 1);
        edit_measure_t m;
        edit_measure_init(&m, &d.doc);
        edit_measure_set_wrap(&m, 5);
        for (int y = 0; y < 5; ++y) {
            edit_cursor_t b = edit_measure_goto_visual(&m, pt(0, y));
            edit_cursor_t e = edit_measure_goto_visual(&m, pt(5, y));
            size_t n = e.offset - b.offset;
            CHECK(n == strlen(exp[y]));
            CHECK(memcmp(t + b.offset, exp[y], n) == 0);
        }
    }
    // test_wrap_tab: "foo \t b", wrap 8, tab 4
    {
        edit_slice_doc_t d;
        edit_slice_doc_init(&d, (const uint8_t *)"foo \t b", 7);
        edit_measure_t m;
        edit_measure_init(&m, &d.doc);
        edit_measure_set_wrap(&m, 8);
        edit_measure_set_tab(&m, 4);
        CHECK(expect_cursor(edit_measure_goto_visual(&m, pt(INT32_MAX, 0)), 4, 4, 0, 4, 0, 4,
                            true) == 0);
        CHECK(expect_cursor(edit_measure_goto_visual(&m, pt(0, 1)), 4, 4, 0, 0, 1, 4, false) == 0);
        CHECK(expect_cursor(edit_measure_goto_visual(&m, pt(INT32_MAX, 1)), 7, 7, 0, 6, 1, 10,
                            true) == 0);
    }
    // test_crlf: "a\r\nbcd\r\ne"
    {
        edit_slice_doc_t d;
        edit_slice_doc_init(&d, (const uint8_t *)"a\r\nbcd\r\ne", 8);
        edit_measure_t m;
        edit_measure_init(&m, &d.doc);
        CHECK(expect_cursor(edit_measure_goto_visual(&m, pt(INT32_MAX, 1)), 6, 3, 1, 3, 1, 3,
                            false) == 0);
    }
    // test_wrapped_cursor_can_seek_backward: "hello world", wrap 10
    {
        edit_slice_doc_t d;
        edit_slice_doc_init(&d, (const uint8_t *)"hello world", 11);
        edit_measure_t m;
        edit_measure_init(&m, &d.doc);
        edit_measure_set_wrap(&m, 10);
        CHECK(expect_cursor(edit_measure_goto_visual(&m, pt(2, 1)), 8, 8, 0, 2, 1, 8, false) == 0);
    }
    // test_newlines_and_strip
    {
        static const uint8_t t[] = "line1\nline2\r\nline3";
        size_t o = 0;
        int ln = 0;
        edit_newlines_forward(t, 18, 0, 0, 2, &o, &ln);
        CHECK(o == 13 && ln == 2);
        edit_newlines_forward(t, 18, 0, 0, 0, &o, &ln);
        CHECK(o == 0 && ln == 0);
        edit_newlines_forward(t, 18, 100, 2, 100, &o, &ln);
        CHECK(o == 18 && ln == 2);
        edit_newlines_backward(t, 18, 18, 2, 1, &o, &ln);
        CHECK(o == 6 && ln == 1);
        edit_newlines_backward(t, 18, 18, 2, 0, &o, &ln);
        CHECK(o == 0 && ln == 0);
        edit_newlines_backward(t, 18, 100, 2, 1, &o, &ln);
        CHECK(o == 6 && ln == 1);
    }
    // test_strip_newline + skip_newline
    {
        CHECK(edit_strip_newline((const uint8_t *)"hello\n", 6) == 5);
        CHECK(edit_strip_newline((const uint8_t *)"hello\r\n", 7) == 5);
        CHECK(edit_strip_newline((const uint8_t *)"hello", 5) == 5);
        CHECK(edit_strip_newline(NULL, 0) == 0);
        CHECK(edit_skip_newline((const uint8_t *)"a\r\nb", 4, 1) == 3);
        CHECK(edit_skip_newline((const uint8_t *)"a\nb", 3, 1) == 2);
        CHECK(edit_skip_newline((const uint8_t *)"ab", 2, 5) == 5);
        CHECK(edit_skip_newline(NULL, 0, 0) == 0);
    }
    // goto_offset + NULL safety
    {
        edit_slice_doc_t d;
        edit_slice_doc_init(&d, (const uint8_t *)"foo\nbar", 7);
        edit_measure_t m;
        edit_measure_init(&m, &d.doc);
        CHECK(expect_cursor(edit_measure_goto_offset(&m, 5), 5, 1, 1, 1, 1, 1, false) == 0);
        edit_cursor_t z = edit_measure_goto_offset(NULL, 5);
        CHECK(z.offset == 0);
        edit_measure_set_tab(NULL, 4);
        edit_measure_set_wrap(NULL, 4);
        edit_measure_set_cursor(NULL, z);
        edit_measure_init(NULL, NULL);
        CHECK(edit_measure_cursor(NULL).offset == 0);
    }

    printf("test_measure: %d checks passed\n", checks);
    return 0;
}
