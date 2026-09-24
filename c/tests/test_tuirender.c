#define _GNU_SOURCE // memmem; must precede headers

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "edit/fb.h"
#include "edit/helpers.h"
#include "edit/input.h"
#include "edit/tree.h"
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

static int expect_render(edit_tui_t *tui, const char *want) {
    const char *got = NULL;
    size_t n = edit_tui_render(tui, &got);
    size_t wlen = strlen(want);
    ++checks;
    if (n != wlen || got == NULL || memcmp(got, want, n) != 0) {
        fprintf(stderr, "FAIL %d: render len %zu != %zu\n", __LINE__, n, wlen);
        if (got != NULL) {
            fprintf(stderr, "got: %.*s\n", (int)(n > 300 ? 300 : n), got);
        }
        return 1;
    }
    return 0;
}

static edit_size_t sz(int w, int h) {
    edit_size_t s = {w, h};
    return s;
}

static void frame_size(edit_tui_t *tui, int w, int h) {
    edit_input_t in;
    memset(&in, 0, sizeof in);
    in.kind = EDIT_IN_RESIZE;
    in.size = sz(w, h);
    edit_ctx_t ctx;
    edit_tui_begin(tui, &in, &ctx);
    edit_tui_end(tui, &ctx);
}

int main(void) {
    edit_tui_t tui;
    CHECK(edit_tui_init(&tui) == 0);
    frame_size(&tui, 20, 6);

    // Labels + styled spans + button (Rust vectors, byte-exact).
    {
        edit_ctx_t ctx;
        CHECK(edit_tui_begin(&tui, NULL, &ctx) == 0);
        edit_ctx_label(&ctx, "title", "hello");
        edit_ctx_styled_begin(&ctx, "st");
        edit_ctx_styled_add(&ctx, "ab", 2);
        edit_ctx_styled_fg(&ctx, 0xFF212CBE);
        edit_ctx_styled_add(&ctx, "cd", 2);
        edit_ctx_styled_end(&ctx);
        bool hit = edit_ctx_button(&ctx, "ok", "OK");
        CHECK(!hit);
        edit_tui_end(&tui, &ctx);
        CHECK(expect_render(&tui,
                            "\x1b[m\x1b[1;1H\x1b[48;2;0;0;0m\x1b[38;2;255;255;255mhello           "
                            "    \x1b[2;1Hab\x1b[38;2;190;44;33mcd"
                            "\x1b[38;2;255;255;255m                \x1b[3;1H[OK]                "
                            "\x1b[4;1H                    \x1b[5;1H                    "
                            "\x1b[6;1H                    \x1b[?25l") == 0);
    }

    // List with default first-item marker (Rust vectors).
    {
        edit_ctx_t ctx;
        CHECK(edit_tui_begin(&tui, NULL, &ctx) == 0);
        edit_ctx_list_begin(&ctx, "files");
        int s0 = edit_ctx_list_item(&ctx, false, "alpha");
        int s1 = edit_ctx_list_item(&ctx, false, "beta");
        CHECK(s0 == 0 && s1 == 0);
        edit_ctx_list_end(&ctx);
        edit_tui_end(&tui, &ctx);
        CHECK(expect_render(&tui,
                            "\x1b[m\x1b[1;1H\x1b[48;2;0;0;0m\x1b[38;2;255;255;255m> alpha         "
                            "    \x1b[2;1H  beta              \x1b[3;1H                    "
                            "\x1b[?25l") == 0);
    }

    // List mouse click selects, arrows move with wrap (Rust vectors).
    {
        edit_ctx_t ctx;
        // Settle first so the click key is not born consumed.
        for (int i = 0; i < 30 && edit_tui_needs_settling(&tui); ++i) {
            CHECK(edit_tui_begin(&tui, NULL, &ctx) == 0);
            edit_ctx_list_begin(&ctx, "files");
            edit_ctx_list_item(&ctx, false, "alpha");
            edit_ctx_list_item(&ctx, false, "beta");
            edit_ctx_list_end(&ctx);
            edit_tui_end(&tui, &ctx);
        }
        // Sync the framebuffer so the click diff is deterministic.
        {
            const char *sync_out = NULL;
            edit_tui_render(&tui, &sync_out);
        }
        // Click beta: mouse down + release at (0,1).
        edit_input_t down;
        memset(&down, 0, sizeof down);
        down.kind = EDIT_IN_MOUSE;
        down.mouse.state = EDIT_MOUSE_LEFT;
        down.mouse.position.x = 0;
        down.mouse.position.y = 1;
        CHECK(edit_tui_begin(&tui, &down, &ctx) == 0);
        edit_ctx_list_begin(&ctx, "files");
        edit_ctx_list_item(&ctx, false, "alpha");
        edit_ctx_list_item(&ctx, false, "beta");
        edit_ctx_list_end(&ctx);
        edit_tui_end(&tui, &ctx);
        edit_input_t up;
        memset(&up, 0, sizeof up);
        up.kind = EDIT_IN_MOUSE;
        up.mouse.state = EDIT_MOUSE_RELEASE;
        up.mouse.position.x = 0;
        up.mouse.position.y = 1;
        CHECK(edit_tui_begin(&tui, &up, &ctx) == 0);
        edit_ctx_list_begin(&ctx, "files");
        edit_ctx_list_item(&ctx, false, "alpha");
        edit_ctx_list_item(&ctx, false, "beta");
        edit_ctx_list_end(&ctx);
        edit_tui_end(&tui, &ctx);
        for (int i = 0; i < 30 && edit_tui_needs_settling(&tui); ++i) {
            CHECK(edit_tui_begin(&tui, NULL, &ctx) == 0);
            edit_ctx_list_begin(&ctx, "files");
            edit_ctx_list_item(&ctx, false, "alpha");
            edit_ctx_list_item(&ctx, false, "beta");
            edit_ctx_list_end(&ctx);
            edit_tui_end(&tui, &ctx);
        }
        CHECK(expect_render(&tui,
                            "\x1b[m\x1b[1;1H\x1b[48;2;0;0;0m\x1b[38;2;255;255;255m  alpha         "
                            "    \x1b[2;1H\x1b[48;2;63;174;58m\x1b[38;2;0;0;0m> beta              "
                            "\x1b[?25l") == 0);
        // DOWN wraps beta -> alpha (minimal diff).
        edit_input_t key;
        memset(&key, 0, sizeof key);
        key.kind = EDIT_IN_KEYBOARD;
        key.key = EDIT_VK_DOWN;
        CHECK(edit_tui_begin(&tui, &key, &ctx) == 0);
        edit_ctx_list_begin(&ctx, "files");
        edit_ctx_list_item(&ctx, false, "alpha");
        edit_ctx_list_item(&ctx, false, "beta");
        edit_ctx_list_end(&ctx);
        edit_tui_end(&tui, &ctx);
        CHECK(expect_render(&tui,
                            "\x1b[m\x1b[1;1H\x1b[48;2;63;174;58m\x1b[38;2;0;0;0m> alpha           "
                            "  \x1b[2;1H\x1b[48;2;0;0;0m\x1b[38;2;255;255;255m  beta              "
                            "\x1b[?25l") == 0);
        for (int i = 0; i < 30 && edit_tui_needs_settling(&tui); ++i) {
            CHECK(edit_tui_begin(&tui, NULL, &ctx) == 0);
            edit_ctx_list_begin(&ctx, "files");
            edit_ctx_list_item(&ctx, false, "alpha");
            edit_ctx_list_item(&ctx, false, "beta");
            edit_ctx_list_end(&ctx);
            edit_tui_end(&tui, &ctx);
        }
        // UP wraps alpha -> beta (minimal diff).
        memset(&key, 0, sizeof key);
        key.kind = EDIT_IN_KEYBOARD;
        key.key = EDIT_VK_UP;
        CHECK(edit_tui_begin(&tui, &key, &ctx) == 0);
        edit_ctx_list_begin(&ctx, "files");
        edit_ctx_list_item(&ctx, false, "alpha");
        edit_ctx_list_item(&ctx, false, "beta");
        edit_ctx_list_end(&ctx);
        edit_tui_end(&tui, &ctx);
        CHECK(expect_render(&tui,
                            "\x1b[m\x1b[1;1H\x1b[48;2;0;0;0m\x1b[38;2;255;255;255m  alpha         "
                            "    \x1b[2;1H\x1b[48;2;63;174;58m\x1b[38;2;0;0;0m> beta              "
                            "\x1b[?25l") == 0);
    }

    // Modal renders border + title + dims behind.
    {
        edit_ctx_t ctx;
        CHECK(edit_tui_begin(&tui, NULL, &ctx) == 0);
        edit_ctx_label(&ctx, "behind", "base");
        edit_ctx_modal_begin(&ctx, "dlg", "Title");
        edit_ctx_label(&ctx, "body", "wider body text");
        bool exit = edit_ctx_modal_end(&ctx);
        CHECK(!exit);
        edit_tui_end(&tui, &ctx);
        // Byte-exact vs the Rust framebuffer (same widgets, 20x6).
        CHECK(expect_render(&tui,
                            "\x1b[m\x1b[1;1H\x1b[48;2;0;0;0m\x1b[38;2;99;99;99mbase                "
                            "\x1b[2;1H \xE2\x94\x8C\xE2\x94\x80 Title \xE2\x94\x80\xE2\x94\x80\xE2"
                            "\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x90  "
                            "\x1b[3;1H \xE2\x94\x82wider body text\xE2\x94\x82  "
                            "\x1b[4;1H \xE2\x94\x94\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80"
                            "\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94"
                            "\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2"
                            "\x94\x98  \x1b[5;1H                    "
                            "\x1b[6;1H                    \x1b[?25l") == 0);
    }

    // Scrollarea renders children + track.
    {
        edit_ctx_t ctx;
        CHECK(edit_tui_begin(&tui, NULL, &ctx) == 0);
        edit_ctx_scrollarea_begin(&ctx, "scroll", sz(10, 3));
        for (int i = 0; i < 8; ++i) {
            char name[16];
            snprintf(name, sizeof name, "l%d", i);
            edit_ctx_label(&ctx, name, "row");
        }
        edit_ctx_scrollarea_end(&ctx);
        edit_tui_end(&tui, &ctx);
        const char *got = NULL;
        size_t n = edit_tui_render(&tui, &got);
        CHECK(n > 0 && got != NULL);
        CHECK(memmem(got, n, "row", 3) != NULL);
    }

    // NULL safety.
    CHECK(edit_ctx_list_item(NULL, false, NULL) == 0);
    edit_ctx_list_begin(NULL, NULL);
    edit_ctx_styled_list_item_begin(NULL);
    CHECK(edit_ctx_styled_list_item_end(NULL, false) == 0);
    edit_ctx_list_end(NULL);
    edit_ctx_scrollarea_begin(NULL, NULL, sz(0, 0));
    edit_ctx_scrollarea_scroll_to(NULL, (edit_point_t){0, 0});
    edit_ctx_scrollarea_end(NULL);
    edit_tui_draw(NULL);

    edit_tui_destroy(&tui);
    printf("test_tuirender: %d checks passed\n", checks);
    return 0;
}
