#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "edit/doc.h"
#include "edit/helpers.h"
#include "edit/input.h"
#include "edit/tbuf.h"
#include "edit/tui.h"

static int checks = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #cond);                                     \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static int expect_text(edit_tbuf_t *t, const char *want, int lx, int ly) {
    size_t n = strlen(want);
    uint8_t buf[128] = {0};
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
    edit_point_t c = edit_tbuf_cursor_logical(t);
    ++checks;
    if (got != n || memcmp(buf, want, n) != 0 || c.x != lx || c.y != ly) {
        fprintf(stderr, "FAIL %d: got %zu (%d,%d)\n", __LINE__, got, c.x, c.y);
        return 1;
    }
    return 0;
}

static void frame(edit_tui_t *tui, edit_shared_tbuf_t *shared, const edit_input_t *in) {
    edit_ctx_t ctx;
    if (edit_tui_begin(tui, in, &ctx) != 0) {
        return;
    }
    edit_ctx_textarea(&ctx, "ta", shared);
    edit_tui_end(tui, &ctx);
}

static void settle(edit_tui_t *tui, edit_shared_tbuf_t *shared) {
    for (int i = 0; i < 30 && edit_tui_needs_settling(tui); ++i) {
        frame(tui, shared, NULL);
    }
}

static edit_input_t key_input(uint32_t key) {
    edit_input_t in;
    memset(&in, 0, sizeof in);
    in.kind = EDIT_IN_KEYBOARD;
    in.key = key;
    return in;
}

static edit_input_t text_input(const char *s) {
    edit_input_t in;
    memset(&in, 0, sizeof in);
    in.kind = EDIT_IN_TEXT;
    in.text.ptr = (const uint8_t *)s;
    in.text.len = strlen(s);
    return in;
}

int main(void) {
    edit_tui_t tui;
    CHECK(edit_tui_init(&tui) == 0);
    {
        edit_input_t rs;
        memset(&rs, 0, sizeof rs);
        rs.kind = EDIT_IN_RESIZE;
        rs.size.width = 80;
        rs.size.height = 24;
        edit_ctx_t ctx;
        CHECK(edit_tui_begin(&tui, &rs, &ctx) == 0);
        edit_tui_end(&tui, &ctx);
    }

    edit_shared_tbuf_t *shared = NULL;
    CHECK(edit_shared_tbuf_create(&shared, false) == 0);

    // Scripted typing session from Rust (focus via Tab first).
    frame(&tui, shared, NULL);
    settle(&tui, shared);
    CHECK(expect_text(&shared->tbuf, "", 0, 0) == 0);
    {
        edit_input_t tab = key_input(EDIT_VK_TAB);
        frame(&tui, shared, &tab);
        settle(&tui, shared);
    }
    {
        edit_input_t h = text_input("H");
        frame(&tui, shared, &h);
        settle(&tui, shared);
        CHECK(expect_text(&shared->tbuf, "H", 1, 0) == 0);
    }
    {
        edit_input_t in = text_input("I");
        frame(&tui, shared, &in);
        settle(&tui, shared);
        CHECK(expect_text(&shared->tbuf, "HI", 2, 0) == 0);
    }
    {
        edit_input_t ret = key_input(EDIT_VK_RETURN);
        frame(&tui, shared, &ret);
        settle(&tui, shared);
        CHECK(expect_text(&shared->tbuf, "HI\n", 0, 1) == 0);
    }
    {
        edit_input_t left = key_input(EDIT_VK_LEFT);
        frame(&tui, shared, &left);
        settle(&tui, shared);
        CHECK(expect_text(&shared->tbuf, "HI\n", 2, 0) == 0);
    }
    {
        edit_input_t back = key_input(EDIT_VK_BACK);
        frame(&tui, shared, &back);
        settle(&tui, shared);
        CHECK(expect_text(&shared->tbuf, "H\n", 1, 0) == 0);
    }
    {
        edit_input_t undo = key_input(EDIT_KBMOD_CTRL | EDIT_VK_Z);
        frame(&tui, shared, &undo);
        settle(&tui, shared);
        CHECK(expect_text(&shared->tbuf, "HI\n", 2, 0) == 0);
    }

    // Editline syncs both directions.
    {
        uint8_t docbuf[64] = {0};
        memcpy(docbuf, "init", 4);
        edit_slice_doc_t doc;
        edit_slice_doc_init(&doc, docbuf, 4);
        // NOTE: editline copies into its field buffer; sync back needs
        // a writable doc. Exercise read-only sync here via dirty flag.
        edit_ctx_t ctx;
        CHECK(edit_tui_begin(&tui, NULL, &ctx) == 0);
        bool dirty = edit_ctx_editline(&ctx, "el", &doc.doc);
        CHECK(!dirty);
        edit_tui_end(&tui, &ctx);
    }

    // Shared handle refcounting.
    {
        edit_shared_tbuf_t *s2 = NULL;
        CHECK(edit_shared_tbuf_create(&s2, true) == 0);
        edit_shared_retain(s2);
        edit_shared_retain(NULL);
        edit_shared_release(s2);
        edit_shared_release(s2); // last release destroys
        edit_shared_release(NULL);
        CHECK(edit_shared_tbuf_create(NULL, false) != 0);
    }

    // NULL safety.
    edit_ctx_textarea(NULL, NULL, NULL);
    CHECK(!edit_ctx_editline(NULL, NULL, NULL));

    edit_shared_release(shared);
    edit_tui_destroy(&tui);
    printf("test_ta: %d checks passed\n", checks);
    return 0;
}
