#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "edit/doc.h"
#include "edit/helpers.h"
#include "edit/tbuf.h"

static int checks = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #cond);                                     \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static int expect_text(edit_tbuf_t *t, const char *want) {
    size_t n = strlen(want);
    uint8_t buf[256] = {0};
    size_t got = 0;
    size_t off = 0;
    while (off < edit_tbuf_len(t)) {
        const uint8_t *p = NULL;
        size_t k = 0;
        edit_tbuf_read_fwd(t, off, &p, &k);
        if (p == NULL || k == 0 || got + k > sizeof buf) {
            break;
        }
        memcpy(buf + got, p, k);
        got += k;
        off += k;
    }
    ++checks;
    if (got != n || memcmp(buf, want, n) != 0) {
        fprintf(stderr, "FAIL %d: got %zu want %zu\n", __LINE__, got, n);
        return 1;
    }
    CHECK(edit_tbuf_len(t) == n);
    return 0;
}

static int expect_state(edit_tbuf_t *t, const char *want, int lx, int ly, int lines, bool dirty) {
    if (expect_text(t, want) != 0) {
        return 1;
    }
    edit_point_t c = edit_tbuf_cursor_logical(t);
    ++checks;
    if (c.x != lx || c.y != ly) {
        fprintf(stderr, "FAIL %d: cursor (%d,%d) want (%d,%d)\n", __LINE__, c.x, c.y, lx, ly);
        return 1;
    }
    CHECK(edit_tbuf_logical_lines(t) == lines);
    CHECK(edit_tbuf_visual_lines(t) == lines);
    CHECK(edit_tbuf_is_dirty(t) == dirty);
    return 0;
}

static edit_point_t pt(int x, int y) {
    edit_point_t p = {x, y};
    return p;
}

int main(void) {
    // Scripted scenario from Rust TextBuffer.
    edit_tbuf_t tb;
    CHECK(edit_tbuf_init(&tb, false) == 0);
    CHECK(expect_state(&tb, "", 0, 0, 1, false) == 0);
    edit_tbuf_write(&tb, (const uint8_t *)"hello", 5, false);
    CHECK(expect_state(&tb, "hello", 5, 0, 1, true) == 0);
    edit_tbuf_write(&tb, (const uint8_t *)" world", 6, false);
    CHECK(expect_state(&tb, "hello world", 11, 0, 1, true) == 0);
    edit_tbuf_undo(&tb);
    CHECK(expect_state(&tb, "", 0, 0, 1, false) == 0); // writes coalesced
    edit_tbuf_undo(&tb);
    CHECK(expect_state(&tb, "", 0, 0, 1, false) == 0); // empty stack: no-op
    edit_tbuf_redo(&tb);
    CHECK(expect_state(&tb, "hello world", 11, 0, 1, true) == 0);
    edit_tbuf_write(&tb, (const uint8_t *)"\nsecond line\nthird", 18, false);
    CHECK(expect_state(&tb, "hello world\nsecond line\nthird", 5, 2, 3, true) == 0);
    edit_tbuf_goto_logical(&tb, pt(0, 1));
    edit_tbuf_delete(&tb, EDIT_MOVE_GRAPHEME, 1);
    CHECK(expect_state(&tb, "hello world\necond line\nthird", 0, 1, 3, true) == 0);
    edit_tbuf_undo(&tb);
    CHECK(expect_state(&tb, "hello world\nsecond line\nthird", 0, 1, 3, true) == 0);
    edit_tbuf_write(&tb, (const uint8_t *)"\ttabbed", 7, false);
    CHECK(expect_state(&tb, "hello world\n    tabbedsecond line\nthird", 10, 1, 3, true) == 0);

    // Tab expansion with tab size 4.
    edit_tbuf_t t2;
    CHECK(edit_tbuf_init(&t2, false) == 0);
    edit_tbuf_write(&t2, (const uint8_t *)"a\tb", 3, false);
    CHECK(expect_state(&t2, "a   b", 5, 0, 1, true) == 0);
    CHECK(edit_tbuf_tab_size(&t2) == 4);
    CHECK(edit_tbuf_set_tab_size(&t2, 2));
    CHECK(edit_tbuf_tab_size(&t2) == 2);
    CHECK(!edit_tbuf_set_tab_size(&t2, 2));
    CHECK(edit_tbuf_set_tab_size(&t2, 99)); // clamps to 8
    CHECK(edit_tbuf_tab_size(&t2) == 8);

    // Newline translation + normalization.
    edit_tbuf_t t3;
    CHECK(edit_tbuf_init(&t3, false) == 0);
    edit_tbuf_write(&t3, (const uint8_t *)"line1\r\nline2\rline3\n", 19, false);
    CHECK(expect_state(&t3, "line1\nline2\nline3\n", 0, 3, 4, true) == 0);
    edit_tbuf_normalize_newlines(&t3, true);
    CHECK(expect_state(&t3, "line1\r\nline2\r\nline3\r\n", 0, 3, 4, true) == 0);
    CHECK(edit_tbuf_is_crlf(&t3));
    edit_tbuf_normalize_newlines(&t3, false);
    CHECK(expect_state(&t3, "line1\nline2\nline3\n", 0, 3, 4, true) == 0);
    CHECK(!edit_tbuf_is_crlf(&t3));

    // Word delete + undo restores text and cursor.
    edit_tbuf_goto_offset(&t3, 3);
    edit_tbuf_delete(&t3, EDIT_MOVE_WORD, -1);
    CHECK(expect_state(&t3, "e1\nline2\nline3\n", 0, 0, 4, true) == 0);
    edit_tbuf_undo(&t3);
    CHECK(expect_state(&t3, "line1\nline2\nline3\n", 3, 0, 4, true) == 0);

    // Backspace at start / delete at end are no-ops.
    edit_tbuf_goto_offset(&t3, 0);
    edit_tbuf_delete(&t3, EDIT_MOVE_GRAPHEME, -1);
    CHECK(expect_state(&t3, "line1\nline2\nline3\n", 0, 0, 4, true) == 0);
    edit_tbuf_goto_offset(&t3, edit_tbuf_len(&t3));
    edit_tbuf_delete(&t3, EDIT_MOVE_GRAPHEME, 1);
    CHECK(edit_tbuf_len(&t3) == 18);

    // Raw write keeps tabs verbatim but still translates newlines.
    edit_tbuf_t t4;
    CHECK(edit_tbuf_init(&t4, true) == 0);
    edit_tbuf_write(&t4, (const uint8_t *)"a\tb\rc", 5, true);
    CHECK(expect_text(&t4, "a\tb\nc") == 0);

    // copy_from keeps the first line; truncation marks dirty like Rust.
    {
        edit_slice_doc_t src;
        edit_slice_doc_init(&src, (const uint8_t *)"first\nsecond", 12);
        edit_tbuf_copy_from(&t4, &src.doc);
        CHECK(expect_text(&t4, "first") == 0);
        CHECK(edit_tbuf_is_dirty(&t4));
        edit_slice_doc_init(&src, (const uint8_t *)"single", 6);
        edit_tbuf_copy_from(&t4, &src.doc);
        CHECK(expect_text(&t4, "single") == 0);
        CHECK(!edit_tbuf_is_dirty(&t4));
        edit_tbuf_write(&t4, (const uint8_t *)"!", 1, true);
        CHECK(edit_tbuf_is_dirty(&t4));
    }

    // save_to round-trips content into another doc and marks clean.
    {
        edit_gap_t dst;
        CHECK(edit_gap_init(&dst, true) == 0);
        edit_doc_t ddoc;
        edit_gap_doc(&dst, &ddoc);
        edit_tbuf_save_to(&t3, &ddoc);
        CHECK(!edit_tbuf_is_dirty(&t3));
        uint8_t buf[64];
        size_t n = edit_gap_extract(&dst, 0, (size_t)(-1), buf, sizeof buf);
        CHECK(n == edit_tbuf_len(&t3));
        // t3 currently holds "line1\nline2\nline3\n" (18 bytes).
        CHECK(n == 18 && memcmp(buf, "line1\nline2\nline3\n", 18) == 0);
        edit_gap_destroy(&dst);
    }

    // Cursor movement basics + selection clearing.
    edit_tbuf_goto_offset(&t3, 0);
    edit_tbuf_move_delta(&t3, EDIT_MOVE_GRAPHEME, 4);
    CHECK(edit_tbuf_cursor_offset(&t3) == 4);
    edit_tbuf_move_delta(&t3, EDIT_MOVE_WORD, 1);
    CHECK(edit_tbuf_cursor_offset(&t3) == 5);
    edit_tbuf_goto_visual(&t3, pt(2, 2));
    edit_point_t v = edit_tbuf_cursor_visual(&t3);
    CHECK(v.x == 2 && v.y == 2);

    // Config + misc accessors.
    CHECK(edit_tbuf_margin_width(&t3) == 0);
    CHECK(edit_tbuf_text_width(&t3) == 0);
    CHECK(edit_tbuf_set_width(&t3, 80));
    CHECK(edit_tbuf_text_width(&t3) == 80);
    CHECK(!edit_tbuf_set_width(&t3, 80));
    CHECK(!edit_tbuf_set_width(&t3, -1));
    edit_tbuf_make_visible(&t3);
    CHECK(edit_tbuf_take_visibility(&t3));
    CHECK(!edit_tbuf_take_visibility(&t3));
    CHECK(strcmp(edit_tbuf_encoding(&t3), "UTF-8") == 0);
    edit_tbuf_set_encoding(&t3, "ISO-8859-1");
    CHECK(strcmp(edit_tbuf_encoding(&t3), "ISO-8859-1") == 0);
    CHECK(!edit_tbuf_is_overtype(&t3));
    edit_tbuf_set_overtype(&t3, true);
    CHECK(edit_tbuf_is_overtype(&t3));
    CHECK(!edit_tbuf_indent_with_tabs(&t3));
    edit_tbuf_set_indent_tabs(&t3, true);
    CHECK(edit_tbuf_indent_with_tabs(&t3));
    CHECK(!edit_tbuf_has_selection(&t3));
    edit_tbuf_set_selection(&t3, pt(0, 0), pt(1, 0));
    CHECK(edit_tbuf_has_selection(&t3));
    edit_tbuf_clear_selection(&t3);
    CHECK(!edit_tbuf_has_selection(&t3));
    CHECK(edit_tbuf_generation(&t3) != 0);
    edit_tbuf_mark_dirty(&t3);
    CHECK(edit_tbuf_is_dirty(&t3));

    // Indent ops (Rust-verified; empty-line unindent is a C no-op).
    {
        edit_tbuf_t it;
        CHECK(edit_tbuf_init(&it, false) == 0);
        edit_tbuf_write(&it, (const uint8_t *)"    indented\n\tTabbed\nplain\n", 27, false);
        edit_tbuf_goto_logical(&it, pt(6, 0));
        edit_tbuf_unindent(&it);
        CHECK(expect_text(&it, "indented\n        Tabbed\n        plain\n        ") == 0);
        edit_point_t c = edit_tbuf_cursor_logical(&it);
        CHECK(c.x == 2 && c.y == 0);
        edit_tbuf_goto_logical(&it, pt(0, 1));
        edit_tbuf_unindent(&it);
        CHECK(expect_text(&it, "indented\n    Tabbed\n        plain\n        ") == 0);
        edit_tbuf_goto_logical(&it, pt(0, 2));
        edit_tbuf_unindent(&it);
        CHECK(expect_text(&it, "indented\n    Tabbed\n    plain\n        ") == 0);
        edit_point_t ie = edit_tbuf_indent_end(&it);
        CHECK(ie.x == 4 && ie.y == 2);
        // Undo restores the removed indentation.
        edit_tbuf_undo(&it);
        CHECK(expect_text(&it, "indented\n    Tabbed\n        plain\n        ") == 0);
        // Empty line: no-op (Rust panics here).
        edit_tbuf_t ie2;
        CHECK(edit_tbuf_init(&ie2, false) == 0);
        edit_tbuf_write(&ie2, (const uint8_t *)"a\n\nb", 4, false);
        edit_tbuf_goto_logical(&ie2, pt(0, 1));
        edit_tbuf_unindent(&ie2);
        CHECK(expect_text(&ie2, "a\n\nb") == 0);
        edit_tbuf_destroy(&ie2);
        edit_tbuf_destroy(&it);
    }

    // NULL safety.
    CHECK(edit_tbuf_init(NULL, false) != 0);
    edit_tbuf_destroy(NULL);
    CHECK(edit_tbuf_len(NULL) == 0);
    edit_tbuf_write(NULL, NULL, 0, false);
    edit_tbuf_delete(NULL, EDIT_MOVE_GRAPHEME, 1);
    edit_tbuf_undo(NULL);
    edit_tbuf_redo(NULL);

    edit_tbuf_destroy(&tb);
    edit_tbuf_destroy(&t2);
    edit_tbuf_destroy(&t3);
    edit_tbuf_destroy(&t4);
    printf("test_tbuf: %d checks passed\n", checks);
    return 0;
}
