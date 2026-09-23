#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "edit/icu.h"
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
    return 0;
}

static int expect_sel(edit_tbuf_t *t, bool has, size_t beg, size_t end) {
    edit_cursor_t b;
    edit_cursor_t e;
    bool got = edit_tbuf_selection_range(t, &b, &e);
    ++checks;
    if (got != has || (has && (b.offset != beg || e.offset != end))) {
        fprintf(stderr, "FAIL %d: sel %d %zu..%zu\n", __LINE__, (int)got, b.offset, e.offset);
        return 1;
    }
    CHECK(edit_tbuf_has_selection(t) == has);
    return 0;
}

static edit_search_opts_t opts(bool mc, bool ww, bool re) {
    edit_search_opts_t o = {mc, ww, re};
    return o;
}

int main(void) {
    if (!edit_icu_available()) {
        fprintf(stderr, "note: libicu missing, skipping\n");
        printf("test_tsearch: %d checks passed\n", checks);
        return 0;
    }
    edit_tbuf_t tb;
    CHECK(edit_tbuf_init(&tb, false) == 0);
    edit_tbuf_write(&tb, (const uint8_t *)"hello world, hello moon", 23, false);
    edit_search_opts_t plain = opts(true, false, false);

    // Vectors from Rust find_and_select (pre-hang, valid).
    CHECK(edit_tbuf_find_select(&tb, "hello", plain) == 0);
    CHECK(expect_sel(&tb, true, 0, 5) == 0);
    CHECK(edit_tbuf_cursor_offset(&tb) == 5);
    CHECK(edit_tbuf_find_select(&tb, "hello", plain) == 0);
    CHECK(expect_sel(&tb, true, 13, 18) == 0);
    CHECK(edit_tbuf_find_select(&tb, "hello", plain) == 0);
    CHECK(expect_sel(&tb, true, 0, 5) == 0); // wrapped around
    // Replace hits the selected match, then selects the next one.
    CHECK(edit_tbuf_find_replace(&tb, "hello", plain, "bye") == 0);
    CHECK(expect_text(&tb, "bye world, hello moon") == 0);
    CHECK(expect_sel(&tb, true, 11, 16) == 0);
    // Whole-word, case-insensitive.
    CHECK(edit_tbuf_find_select(&tb, "bye", opts(false, true, false)) == 0);
    CHECK(expect_sel(&tb, true, 0, 3) == 0);

    // Replace-all terminates (upstream loops forever on stale chunks).
    edit_tbuf_t tb2;
    CHECK(edit_tbuf_init(&tb2, false) == 0);
    edit_tbuf_write(&tb2, (const uint8_t *)"hello", 5, false);
    CHECK(edit_tbuf_find_replace_all(&tb2, "l", plain, "L") == 0);
    CHECK(expect_text(&tb2, "heLLo") == 0);
    // No-match replace-all is a no-op.
    CHECK(edit_tbuf_find_replace_all(&tb2, "zzz", plain, "q") == 0);
    CHECK(expect_text(&tb2, "heLLo") == 0);

    // Bad pattern reports an error (regex mode; literal "(" is fine).
    CHECK(edit_tbuf_find_select(&tb2, "(", opts(true, false, true)) != 0);
    CHECK(edit_tbuf_find_select(&tb2, "(", plain) == 0);

    // Empty pattern clears an active search selection.
    // Note: the earlier replace-all leaves the cursor at 4, so the append
    // lands mid-text (Rust-verified: "heLLhello worldo").
    edit_tbuf_write(&tb2, (const uint8_t *)"hello world", 11, true);
    CHECK(expect_text(&tb2, "heLLhello worldo") == 0);
    CHECK(edit_tbuf_find_select(&tb2, "world", plain) == 0);
    CHECK(expect_sel(&tb2, true, 10, 15) == 0);
    CHECK(edit_tbuf_find_select(&tb2, "", plain) == 0);
    CHECK(edit_tbuf_cursor_offset(&tb2) == 10); // back to selection start

    // Selection ops (Rust-verified ranges).
    edit_tbuf_goto_offset(&tb2, 1);
    edit_tbuf_select_word(&tb2);
    CHECK(expect_sel(&tb2, true, 0, 9) == 0);
    edit_tbuf_select_line(&tb2);
    CHECK(expect_sel(&tb2, true, 0, 16) == 0);
    edit_tbuf_select_all(&tb2);
    CHECK(expect_sel(&tb2, true, 0, 16) == 0);
    // Extract with line fallback when nothing is selected.
    edit_tbuf_clear_selection(&tb2);
    {
        size_t n = 0;
        uint8_t *s = edit_tbuf_extract_selection(&tb2, false, &n);
        CHECK(n == 16 && memcmp(s, "heLLhello worldo", 16) == 0);
        free(s);
    }
    // Extract + delete a real selection (undoable).
    edit_tbuf_select_all(&tb2);
    {
        size_t n = 0;
        uint8_t *s = edit_tbuf_extract_selection(&tb2, true, &n);
        CHECK(n == 16);
        free(s);
        CHECK(expect_text(&tb2, "") == 0);
        edit_tbuf_undo(&tb2);
        CHECK(expect_text(&tb2, "heLLhello worldo") == 0);
    }
    // User selection refuses search-made selections.
    edit_tbuf_goto_offset(&tb2, 0);
    CHECK(edit_tbuf_find_select(&tb2, "hello", plain) == 0);
    CHECK(expect_sel(&tb2, true, 4, 9) == 0);
    {
        size_t n = 0;
        bool has = true;
        uint8_t *s = edit_tbuf_extract_user_selection(&tb2, false, &n, &has);
        CHECK(!has && s == NULL);
    }
    edit_tbuf_goto_offset(&tb2, 2);
    edit_tbuf_start_selection(&tb2);
    edit_tbuf_selection_update_logical(&tb2, (edit_point_t){4, 0});
    {
        size_t n = 0;
        bool has = false;
        uint8_t *s = edit_tbuf_extract_user_selection(&tb2, false, &n, &has);
        CHECK(has && n == 2);
        free(s);
    }

    // NULL safety.
    CHECK(edit_tbuf_find_select(NULL, "a", plain) != 0);
    CHECK(edit_tbuf_find_select(&tb2, NULL, plain) != 0);
    CHECK(edit_tbuf_find_replace(NULL, "a", plain, "b") != 0);
    CHECK(edit_tbuf_find_replace_all(NULL, "a", plain, "b") != 0);
    edit_tbuf_select_word(NULL);
    edit_tbuf_select_line(NULL);
    edit_tbuf_select_all(NULL);
    edit_tbuf_start_selection(NULL);
    edit_tbuf_selection_update_visual(NULL, (edit_point_t){0, 0});

    edit_tbuf_destroy(&tb);
    edit_tbuf_destroy(&tb2);
    printf("test_tsearch: %d checks passed\n", checks);
    return 0;
}
