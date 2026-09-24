#define _GNU_SOURCE // memmem; must precede headers

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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

static void frame_size(edit_tui_t *tui, int w, int h) {
    edit_input_t in;
    memset(&in, 0, sizeof in);
    in.kind = EDIT_IN_RESIZE;
    in.size.width = w;
    in.size.height = h;
    edit_ctx_t ctx;
    edit_tui_begin(tui, &in, &ctx);
    edit_tui_end(tui, &ctx);
}

// Minimal app-like menubar: File menu with New/Open buttons.
static void build_menu(edit_ctx_t *ctx, bool *got_new, bool *got_open) {
    edit_ctx_menubar_begin(ctx);
    if (edit_ctx_menubar_menu_begin(ctx, "File", 'F')) {
        if (edit_ctx_menubar_menu_button(ctx, "New", 'N', EDIT_KBMOD_CTRL | 0x4E)) {
            *got_new = true;
        }
        if (edit_ctx_menubar_menu_button(ctx, "Open", 'O', EDIT_KBMOD_CTRL | 0x4F)) {
            *got_open = true;
        }
        edit_ctx_menubar_menu_end(ctx);
    }
    edit_ctx_menubar_end(ctx);
}

static void settle_menu(edit_tui_t *tui, bool *got_new, bool *got_open) {
    for (int i = 0; i < 30 && edit_tui_needs_settling(tui); ++i) {
        edit_ctx_t ctx;
        if (edit_tui_begin(tui, NULL, &ctx) != 0) {
            break;
        }
        build_menu(&ctx, got_new, got_open);
        edit_tui_end(tui, &ctx);
    }
}

int main(void) {
    edit_tui_t tui;
    CHECK(edit_tui_init(&tui) == 0);
    frame_size(&tui, 40, 10);

    // Closed menubar renders labels, no flyout.
    {
        bool got_new = false, got_open = false;
        edit_ctx_t ctx;
        CHECK(edit_tui_begin(&tui, NULL, &ctx) == 0);
        build_menu(&ctx, &got_new, &got_open);
        edit_tui_end(&tui, &ctx);
        CHECK(!got_new && !got_open);
        settle_menu(&tui, &got_new, &got_open);
        // Render the settled tree directly (no extra build: it would
        // reset the settling counters and swallow the next input).
        const char *out = NULL;
        size_t n = edit_tui_render(&tui, &out);
        CHECK(n > 0 && out != NULL);
        // Accelerator 'F' is underlined: "F" split from "ile" by SGR.
        CHECK(memmem(out, n, "ile", 3) != NULL);
        CHECK(memmem(out, n, "[4m", 3) != NULL);
        // Flyout not open: item labels absent.
        CHECK(memmem(out, n, "Ctrl+N", 6) == NULL);
    }

    // Alt+F opens the menu; shortcut hint shows translated modifiers.
    {
        edit_tui_set_modifiers(&tui, "Ctrl", "Alt", "Shift");
        bool got_new = false, got_open = false;
        edit_input_t alt;
        memset(&alt, 0, sizeof alt);
        alt.kind = EDIT_IN_KEYBOARD;
        alt.key = EDIT_KBMOD_ALT | 0x46;
        edit_ctx_t ctx;
        CHECK(edit_tui_begin(&tui, &alt, &ctx) == 0);
        build_menu(&ctx, &got_new, &got_open);
        edit_tui_end(&tui, &ctx);
        CHECK(!got_new && !got_open);
        settle_menu(&tui, &got_new, &got_open);
        const char *out = NULL;
        // Render the settled tree directly (see above).
        size_t n = edit_tui_render(&tui, &out);
        CHECK(n > 0 && out != NULL);
        // Item accelerator underlined: "ew" present, "N" split by SGR.
        CHECK(memmem(out, n, "ew", 2) != NULL);
        CHECK(memmem(out, n, "Ctrl+N", 6) != NULL);
    }

    // Accelerator 'N' activates the New button.
    {
        bool got_new = false, got_open = false;
        edit_input_t key;
        memset(&key, 0, sizeof key);
        key.kind = EDIT_IN_KEYBOARD;
        key.key = 0x4E;
        edit_ctx_t ctx;
        CHECK(edit_tui_begin(&tui, &key, &ctx) == 0);
        build_menu(&ctx, &got_new, &got_open);
        edit_tui_end(&tui, &ctx);
        CHECK(got_new && !got_open);
        // Activation clears focus; settle before the next input.
        settle_menu(&tui, &got_new, &got_open);
    }

    // Menu checkbox renders check glyph and toggles via accelerator.
    {
        bool wrap = false;
        edit_input_t alt;
        memset(&alt, 0, sizeof alt);
        alt.kind = EDIT_IN_KEYBOARD;
        alt.key = EDIT_KBMOD_ALT | 0x56;
        edit_ctx_t ctx;
        CHECK(edit_tui_begin(&tui, &alt, &ctx) == 0);
        edit_ctx_menubar_begin(&ctx);
        bool view_open = edit_ctx_menubar_menu_begin(&ctx, "View", 'V');
        CHECK(view_open);
        if (view_open) {
            bool hit = edit_ctx_menubar_menu_checkbox(&ctx, "Word Wrap", 'W', 0, wrap);
            if (hit) {
                wrap = !wrap;
            }
            edit_ctx_menubar_menu_end(&ctx);
        }
        edit_ctx_menubar_end(&ctx);
        edit_tui_end(&tui, &ctx);
        for (int i = 0; i < 30 && edit_tui_needs_settling(&tui); ++i) {
            CHECK(edit_tui_begin(&tui, NULL, &ctx) == 0);
            edit_ctx_menubar_begin(&ctx);
            if (edit_ctx_menubar_menu_begin(&ctx, "View", 'V')) {
                bool hit = edit_ctx_menubar_menu_checkbox(&ctx, "Word Wrap", 'W', 0, wrap);
                if (hit) {
                    wrap = !wrap;
                }
                edit_ctx_menubar_menu_end(&ctx);
            }
            edit_ctx_menubar_end(&ctx);
            edit_tui_end(&tui, &ctx);
        }
        // Toggle via accelerator.
        edit_input_t key;
        memset(&key, 0, sizeof key);
        key.kind = EDIT_IN_KEYBOARD;
        key.key = 0x57;
        CHECK(edit_tui_begin(&tui, &key, &ctx) == 0);
        edit_ctx_menubar_begin(&ctx);
        view_open = edit_ctx_menubar_menu_begin(&ctx, "View", 'V');
        CHECK(view_open);
        if (view_open) {
            bool hit = edit_ctx_menubar_menu_checkbox(&ctx, "Word Wrap", 'W', 0, wrap);
            if (hit) {
                wrap = !wrap;
            }
            edit_ctx_menubar_menu_end(&ctx);
        }
        edit_ctx_menubar_end(&ctx);
        edit_tui_end(&tui, &ctx);
        CHECK(wrap);
        // Settle the activation, then reopen to show the check glyph.
        for (int i = 0; i < 30 && edit_tui_needs_settling(&tui); ++i) {
            CHECK(edit_tui_begin(&tui, NULL, &ctx) == 0);
            edit_ctx_menubar_begin(&ctx);
            if (edit_ctx_menubar_menu_begin(&ctx, "View", 'V')) {
                edit_ctx_menubar_menu_checkbox(&ctx, "Word Wrap", 'W', 0, wrap);
                edit_ctx_menubar_menu_end(&ctx);
            }
            edit_ctx_menubar_end(&ctx);
            edit_tui_end(&tui, &ctx);
        }
        // Reopen to show the check glyph (activation closed the menu).
        memset(&key, 0, sizeof key);
        key.kind = EDIT_IN_KEYBOARD;
        key.key = EDIT_KBMOD_ALT | 0x56;
        CHECK(edit_tui_begin(&tui, &key, &ctx) == 0);
        edit_ctx_menubar_begin(&ctx);
        if (edit_ctx_menubar_menu_begin(&ctx, "View", 'V')) {
            edit_ctx_menubar_menu_checkbox(&ctx, "Word Wrap", 'W', 0, wrap);
            edit_ctx_menubar_menu_end(&ctx);
        }
        edit_ctx_menubar_end(&ctx);
        edit_tui_end(&tui, &ctx);
        for (int i = 0; i < 30 && edit_tui_needs_settling(&tui); ++i) {
            CHECK(edit_tui_begin(&tui, NULL, &ctx) == 0);
            edit_ctx_menubar_begin(&ctx);
            if (edit_ctx_menubar_menu_begin(&ctx, "View", 'V')) {
                edit_ctx_menubar_menu_checkbox(&ctx, "Word Wrap", 'W', 0, wrap);
                edit_ctx_menubar_menu_end(&ctx);
            }
            edit_ctx_menubar_end(&ctx);
            edit_tui_end(&tui, &ctx);
        }
        // Check glyph present in the render.
        const char *out = NULL;
        size_t n = edit_tui_render(&tui, &out);
        CHECK(n > 0 && out != NULL);
        CHECK(memmem(out, n, "▣", 3) != NULL);
    }

    // String editline round-trips typed text.
    {
        char *buf = NULL;
        size_t len = 0, cap = 0;
        edit_ctx_t ctx;
        CHECK(edit_tui_begin(&tui, NULL, &ctx) == 0);
        bool dirty = edit_ctx_editline_str(&ctx, "needle", &buf, &len, &cap);
        CHECK(!dirty && len == 0);
        // Focus the field so typing lands in it.
        edit_ctx_steal_focus(&ctx);
        edit_tui_end(&tui, &ctx);
        for (int i = 0; i < 30 && edit_tui_needs_settling(&tui); ++i) {
            CHECK(edit_tui_begin(&tui, NULL, &ctx) == 0);
            edit_ctx_editline_str(&ctx, "needle", &buf, &len, &cap);
            edit_tui_end(&tui, &ctx);
        }
        // Type "hi" via text input.
        for (int i = 0; i < 2; ++i) {
            edit_input_t text;
            memset(&text, 0, sizeof text);
            text.kind = EDIT_IN_TEXT;
            static const char *letters = "hi";
            text.text.ptr = (const uint8_t *)(letters + i);
            text.text.len = 1;
            CHECK(edit_tui_begin(&tui, &text, &ctx) == 0);
            dirty = edit_ctx_editline_str(&ctx, "needle", &buf, &len, &cap);
            edit_tui_end(&tui, &ctx);
            (void)dirty;
            for (int k = 0; k < 30 && edit_tui_needs_settling(&tui); ++k) {
                CHECK(edit_tui_begin(&tui, NULL, &ctx) == 0);
                edit_ctx_editline_str(&ctx, "needle", &buf, &len, &cap);
                edit_tui_end(&tui, &ctx);
            }
        }
        CHECK(len == 2 && memcmp(buf, "hi", 2) == 0);
        // Steady state: no change reported.
        CHECK(edit_tui_begin(&tui, NULL, &ctx) == 0);
        dirty = edit_ctx_editline_str(&ctx, "needle", &buf, &len, &cap);
        edit_tui_end(&tui, &ctx);
        CHECK(!dirty);
        CHECK(edit_ctx_editline_str(NULL, NULL, NULL, NULL, NULL) == false);
        free(buf);
    }

    // Toss focus up + context clipboard.
    {
        edit_ctx_t ctx;
        for (int i = 0; i < 30 && edit_tui_needs_settling(&tui); ++i) {
            CHECK(edit_tui_begin(&tui, NULL, &ctx) == 0);
            edit_tui_end(&tui, &ctx);
        }
        CHECK(edit_tui_begin(&tui, NULL, &ctx) == 0);
        edit_ctx_block_begin(&ctx, "outer");
        edit_ctx_block_begin(&ctx, "inner");
        edit_ctx_steal_focus(&ctx);
        CHECK(edit_ctx_is_focused(&ctx));
        edit_ctx_toss_focus_up(&ctx);
        edit_ctx_block_end(&ctx);
        edit_ctx_block_end(&ctx);
        edit_tui_end(&tui, &ctx);
        for (int i = 0; i < 30 && edit_tui_needs_settling(&tui); ++i) {
            CHECK(edit_tui_begin(&tui, NULL, &ctx) == 0);
            edit_tui_end(&tui, &ctx);
        }
        const uint8_t clip[] = {'a', 'b'};
        CHECK(edit_tui_begin(&tui, NULL, &ctx) == 0);
        uint32_t g0 = edit_ctx_clipboard_gen(&ctx);
        edit_ctx_set_clipboard(&ctx, clip, sizeof clip);
        CHECK(edit_ctx_clipboard_gen(&ctx) == g0 + 1);
        const uint8_t *got = NULL;
        CHECK(edit_ctx_clipboard(&ctx, &got) == 2 && memcmp(got, "ab", 2) == 0);
        // Empty set is a no-op (mirrors Rust).
        edit_ctx_set_clipboard(&ctx, clip, 0);
        CHECK(edit_ctx_clipboard_gen(&ctx) == g0 + 1);
        edit_ctx_needs_rerender(&ctx);
        edit_ctx_set_consumed(&ctx);
        edit_tui_end(&tui, &ctx);
        CHECK(edit_ctx_clipboard(NULL, NULL) == 0);
        CHECK(edit_ctx_clipboard_gen(NULL) == 0);
        edit_ctx_set_clipboard(NULL, NULL, 0);
        edit_ctx_needs_rerender(NULL);
        edit_ctx_toss_focus_up(NULL);
    }

    // NULL safety for menubar.
    edit_ctx_menubar_begin(NULL);
    CHECK(!edit_ctx_menubar_menu_begin(NULL, NULL, 0));
    CHECK(!edit_ctx_menubar_menu_button(NULL, NULL, 0, 0));
    CHECK(!edit_ctx_menubar_menu_checkbox(NULL, NULL, 0, 0, false));
    edit_ctx_menubar_menu_end(NULL);
    edit_ctx_menubar_end(NULL);
    edit_tui_set_modifiers(NULL, NULL, NULL, NULL);
    edit_tui_set_modifiers(&tui, NULL, NULL, NULL);

    edit_tui_destroy(&tui);
    printf("test_tmenu: %d checks passed\n", checks);
    return 0;
}
