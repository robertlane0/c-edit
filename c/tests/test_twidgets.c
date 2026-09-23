#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

static edit_size_t sz(int w, int h) {
    edit_size_t s = {w, h};
    return s;
}

static void frame_size(edit_tui_t *tui) {
    edit_input_t in;
    memset(&in, 0, sizeof in);
    in.kind = EDIT_IN_RESIZE;
    in.size = sz(80, 24);
    edit_ctx_t ctx;
    edit_tui_begin(tui, &in, &ctx);
    edit_tui_end(tui, &ctx);
}

// Builds the OK button frame; optionally feeds input. Settles like the app.
static bool frame_button(edit_tui_t *tui, const edit_input_t *in, bool *hit) {
    edit_ctx_t ctx;
    if (edit_tui_begin(tui, in, &ctx) != 0) {
        return false;
    }
    bool h = edit_ctx_button(&ctx, "ok", "OK");
    if (hit != NULL) {
        *hit = h;
    }
    edit_tui_end(tui, &ctx);
    for (int i = 0; i < 30 && edit_tui_needs_settling(tui); ++i) {
        edit_ctx_t sctx;
        if (edit_tui_begin(tui, NULL, &sctx) != 0) {
            return false;
        }
        edit_ctx_button(&sctx, "ok", "OK");
        edit_tui_end(tui, &sctx);
    }
    return true;
}

int main(void) {
    edit_tui_t tui;
    CHECK(edit_tui_init(&tui) == 0);
    frame_size(&tui);

    // Label intrinsic width + stored text.
    {
        edit_ctx_t ctx;
        CHECK(edit_tui_begin(&tui, NULL, &ctx) == 0);
        edit_ctx_label(&ctx, "greet", "hello");
        edit_tnode_t *n = ctx.tree.last_node;
        CHECK(n != NULL && n->content_kind == 3);
        CHECK(n->text_len == 5 && memcmp(n->text_ptr, "hello", 5) == 0);
        CHECK(n->intrinsic_set && n->intrinsic_size.width == 5);
        CHECK(n->intrinsic_size.height == 1);
        edit_tui_end(&tui, &ctx);
    }

    // Styled spans with fg dedup + overflow mode.
    {
        edit_ctx_t ctx;
        CHECK(edit_tui_begin(&tui, NULL, &ctx) == 0);
        edit_ctx_styled_begin(&ctx, "styled");
        CHECK(edit_ctx_styled_fg(&ctx, 0xFFFF0000));
        CHECK(edit_ctx_styled_add(&ctx, "ab", 2));
        CHECK(edit_ctx_styled_fg(&ctx, 0xFFFF0000)); // same fg: no new chunk
        CHECK(edit_ctx_styled_add(&ctx, "cd", 2));
        CHECK(edit_ctx_styled_fg(&ctx, 0xFF00FF00));
        CHECK(edit_ctx_styled_add(&ctx, "ef", 2));
        CHECK(edit_ctx_styled_attr(&ctx, EDIT_FB_ATTR_UNDERLINED));
        edit_ctx_set_overflow(&ctx, 3); // tail
        edit_tnode_t *n = ctx.tree.last_node;
        CHECK(n->text_nchunks == 3);
        CHECK(n->text_chunks[0].offset == 0 && n->text_chunks[0].fg == 0xFFFF0000);
        CHECK(n->text_chunks[1].offset == 4 && n->text_chunks[1].fg == 0xFF00FF00);
        CHECK(n->text_chunks[2].offset == 6 && n->text_chunks[2].attr == EDIT_FB_ATTR_UNDERLINED);
        CHECK(n->text_overflow == EDIT_OVF_TAIL);
        edit_ctx_styled_end(&ctx);
        edit_tui_end(&tui, &ctx);
    }

    // Button activates on Return when focused.
    {
        bool hit = true;
        CHECK(frame_button(&tui, NULL, &hit));
        CHECK(!hit); // not focused, no input

        // Focus it via Tab, then press Return.
        edit_input_t tab;
        memset(&tab, 0, sizeof tab);
        tab.kind = EDIT_IN_KEYBOARD;
        tab.key = EDIT_VK_TAB;
        CHECK(frame_button(&tui, &tab, &hit));
        CHECK(!hit); // Tab consumed by focus movement, not the button

        edit_input_t ret;
        memset(&ret, 0, sizeof ret);
        ret.kind = EDIT_IN_KEYBOARD;
        ret.key = EDIT_VK_RETURN;
        CHECK(frame_button(&tui, &ret, &hit));
        CHECK(hit); // focused + Return
    }

    // Checkbox toggles on activation.
    {
        bool checked = false;
        edit_input_t ret;
        memset(&ret, 0, sizeof ret);
        ret.kind = EDIT_IN_KEYBOARD;
        ret.key = EDIT_VK_SPACE;
        // Not focused yet: Space goes nowhere. Each frame rebuilds it.
        for (int round = 0; round < 2; ++round) {
            edit_ctx_t ctx;
            CHECK(edit_tui_begin(&tui, round == 1 ? &ret : NULL, &ctx) == 0);
            bool hit = edit_ctx_checkbox(&ctx, "cb", "Enable", &checked);
            CHECK(!hit && !checked);
            edit_tui_end(&tui, &ctx);
            for (int i = 0; i < 30 && edit_tui_needs_settling(&tui); ++i) {
                edit_ctx_t sctx;
                CHECK(edit_tui_begin(&tui, NULL, &sctx) == 0);
                edit_ctx_checkbox(&sctx, "cb", "Enable", &checked);
                edit_tui_end(&tui, &sctx);
            }
        }
        // Focus via Tab first.
        edit_input_t tab;
        memset(&tab, 0, sizeof tab);
        tab.kind = EDIT_IN_KEYBOARD;
        tab.key = EDIT_VK_TAB;
        {
            edit_ctx_t ctx;
            CHECK(edit_tui_begin(&tui, &tab, &ctx) == 0);
            bool hit = edit_ctx_checkbox(&ctx, "cb", "Enable", &checked);
            CHECK(!hit && !checked);
            edit_tui_end(&tui, &ctx);
            for (int i = 0; i < 30 && edit_tui_needs_settling(&tui); ++i) {
                edit_ctx_t sctx;
                CHECK(edit_tui_begin(&tui, NULL, &sctx) == 0);
                edit_ctx_checkbox(&sctx, "cb", "Enable", &checked);
                edit_tui_end(&tui, &sctx);
            }
        }
        // Now Space toggles.
        {
            edit_ctx_t ctx;
            CHECK(edit_tui_begin(&tui, &ret, &ctx) == 0);
            bool hit = edit_ctx_checkbox(&ctx, "cb", "Enable", &checked);
            CHECK(hit && checked);
            edit_tui_end(&tui, &ctx);
        }
    }

    // Modal structure: dim + centered bordered window with title.
    {
        edit_ctx_t ctx;
        CHECK(edit_tui_begin(&tui, NULL, &ctx) == 0);
        edit_ctx_modal_begin(&ctx, "dlg", "Title");
        edit_tnode_t *win = ctx.tree.last_node;
        CHECK(win != NULL && win->content_kind == 4);
        CHECK(win->modal_title != NULL && strcmp(win->modal_title, " Title ") == 0);
        CHECK(win->attributes.bordered);
        CHECK(win->attributes.float_attr.has_float);
        bool exit = edit_ctx_modal_end(&ctx);
        CHECK(!exit); // no input: stays open
        edit_tui_end(&tui, &ctx);
        // Escape requests close when focused... (unfocused here: false).
        CHECK(!exit);
    }

    // Table structure with columns + rows.
    {
        edit_ctx_t ctx;
        CHECK(edit_tui_begin(&tui, NULL, &ctx) == 0);
        edit_ctx_table_begin(&ctx, "grid");
        edit_tnode_t *tab = ctx.tree.last_node;
        int32_t cols[] = {10, 20};
        CHECK(edit_ctx_table_set_columns(&ctx, cols, 2));
        edit_ctx_table_set_gap(&ctx, sz(1, 0));
        edit_ctx_table_next_row(&ctx);
        edit_ctx_label(&ctx, "c00", "a");
        edit_ctx_label(&ctx, "c01", "b");
        edit_ctx_table_next_row(&ctx);
        edit_ctx_label(&ctx, "c10", "c");
        edit_ctx_table_end(&ctx);
        CHECK(tab != NULL && tab->content_kind == 1);
        CHECK(tab->table_ncols == 2 && tab->table_columns[0] == 10);
        edit_tui_end(&tui, &ctx);
    }

    // Attr setters + input queries + mixin uniqueness.
    {
        edit_ctx_t ctx;
        CHECK(edit_tui_begin(&tui, NULL, &ctx) == 0);
        edit_ctx_block_begin(&ctx, "box");
        edit_ctx_attr_focus_well(&ctx);
        edit_ctx_attr_intrinsic_size(&ctx, sz(7, 3));
        edit_ctx_attr_border(&ctx);
        edit_ctx_attr_position(&ctx, 2);
        edit_rect_t pad = {1, 2, 3, 4};
        edit_ctx_attr_padding(&ctx, pad);
        edit_ctx_attr_bg(&ctx, 0xFF112233);
        edit_ctx_attr_fg(&ctx, 0xFF445566);
        edit_ctx_attr_reverse(&ctx);
        edit_tnode_t *n = ctx.tree.last_node;
        CHECK(n->attributes.focus_well && n->attributes.bordered);
        CHECK(n->attributes.position == EDIT_POS_CENTER);
        CHECK(n->attributes.padding.left == 1 && n->attributes.padding.bottom == 4);
        CHECK(n->attributes.bg == 0xFF112233 && n->attributes.reverse);
        CHECK(n->intrinsic_set && n->intrinsic_size.width == 7);
        // Negative padding clamps to zero.
        edit_rect_t neg = {-5, -5, -5, -5};
        edit_ctx_attr_padding(&ctx, neg);
        CHECK(n->attributes.padding.left == 0);
        edit_ctx_block_end(&ctx);
        // Mixin disambiguates same-class siblings.
        edit_ctx_id_mixin(&ctx, 1);
        edit_ctx_block_begin(&ctx, "item");
        edit_tnode_t *a = ctx.tree.last_node;
        edit_ctx_block_end(&ctx);
        edit_ctx_id_mixin(&ctx, 2);
        edit_ctx_block_begin(&ctx, "item");
        edit_tnode_t *b = ctx.tree.last_node;
        edit_ctx_block_end(&ctx);
        CHECK(a->id != b->id);
        // Shortcuts + consumed flag.
        edit_tui_end(&tui, &ctx);
    }
    {
        for (int i = 0; i < 30 && edit_tui_needs_settling(&tui); ++i) {
            edit_ctx_t sctx;
            CHECK(edit_tui_begin(&tui, NULL, &sctx) == 0);
            edit_tui_end(&tui, &sctx);
        }
        edit_input_t key;
        memset(&key, 0, sizeof key);
        key.kind = EDIT_IN_KEYBOARD;
        key.key = EDIT_VK_RETURN;
        edit_ctx_t ctx;
        CHECK(edit_tui_begin(&tui, &key, &ctx) == 0);
        uint32_t k = 0;
        CHECK(edit_ctx_keyboard_input(&ctx, &k) && k == EDIT_VK_RETURN);
        CHECK(edit_ctx_consume_shortcut(&ctx, EDIT_VK_RETURN));
        CHECK(!edit_ctx_keyboard_input(&ctx, NULL));
        CHECK(!edit_ctx_consume_shortcut(&ctx, EDIT_VK_RETURN));
        edit_tui_end(&tui, &ctx);
    }

    // NULL safety.
    edit_ctx_block_begin(NULL, NULL);
    edit_ctx_block_end(NULL);
    edit_ctx_id_mixin(NULL, 0);
    edit_ctx_label(NULL, NULL, NULL);
    CHECK(!edit_ctx_button(NULL, NULL, NULL));
    {
        bool c = false;
        CHECK(!edit_ctx_checkbox(NULL, NULL, NULL, &c));
        CHECK(!edit_ctx_checkbox(NULL, NULL, NULL, NULL));
    }
    CHECK(!edit_ctx_modal_end(NULL));

    edit_tui_destroy(&tui);
    printf("test_twidgets: %d checks passed\n", checks);
    return 0;
}
