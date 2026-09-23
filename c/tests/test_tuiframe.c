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

static edit_input_t key_input(uint32_t key) {
    edit_input_t in;
    memset(&in, 0, sizeof in);
    in.kind = EDIT_IN_KEYBOARD;
    in.key = key;
    return in;
}

// Builds the two-button tree used below; returns b1/b2 ids.
static void frame_buttons(edit_tui_t *tui, const edit_input_t *in, uint64_t *id1, uint64_t *id2) {
    edit_ctx_t ctx;
    if (edit_tui_begin(tui, in, &ctx) != 0) {
        return;
    }
    edit_tnode_t *b1 = edit_tree_block_begin(&ctx.tree, "b1");
    b1->attributes.focusable = true;
    b1->intrinsic_size.width = 4;
    b1->intrinsic_size.height = 1;
    b1->intrinsic_set = true;
    edit_tree_block_end(&ctx.tree);
    edit_tnode_t *b2 = edit_tree_block_begin(&ctx.tree, "b2");
    b2->attributes.focusable = true;
    b2->intrinsic_size.width = 4;
    b2->intrinsic_size.height = 1;
    b2->intrinsic_set = true;
    edit_tree_block_end(&ctx.tree);
    if (id1 != NULL) {
        *id1 = b1->id;
    }
    if (id2 != NULL) {
        *id2 = b2->id;
    }
    edit_tui_end(tui, &ctx);
}

// Redraws the same UI until the framework settles, like the app loop.
static void settle(edit_tui_t *tui) {
    for (int i = 0; i < 30 && edit_tui_needs_settling(tui); ++i) {
        frame_buttons(tui, NULL, NULL, NULL);
    }
}

static edit_input_t text_input(const char *s) {
    edit_input_t in;
    memset(&in, 0, sizeof in);
    in.kind = EDIT_IN_TEXT;
    in.text.ptr = (const uint8_t *)s;
    in.text.len = strlen(s);
    return in;
}

static edit_input_t mouse_input(int state, int x, int y) {
    edit_input_t in;
    memset(&in, 0, sizeof in);
    in.kind = EDIT_IN_MOUSE;
    in.mouse.state = (edit_mouse_state_t)state;
    in.mouse.position.x = x;
    in.mouse.position.y = y;
    return in;
}

int main(void) {
    edit_tui_t tui;
    CHECK(edit_tui_init(&tui) == 0);
    CHECK(edit_tui_size(&tui).width == 0);

    // Resize input sets the viewport size.
    {
        edit_input_t in;
        memset(&in, 0, sizeof in);
        in.kind = EDIT_IN_RESIZE;
        in.size.width = 80;
        in.size.height = 24;
        edit_ctx_t ctx;
        CHECK(edit_tui_begin(&tui, &in, &ctx) == 0);
        edit_tree_block_begin(&ctx.tree, "root-child");
        edit_tree_block_end(&ctx.tree);
        edit_tui_end(&tui, &ctx);
        CHECK(edit_tui_size(&tui).width == 80);
        CHECK(edit_tui_size(&tui).height == 24);
    }

    // Identical frames produce identical checksums (settling converges).
    {
        edit_ctx_t ctx;
        CHECK(edit_tui_begin(&tui, NULL, &ctx) == 0);
        edit_tree_block_begin(&ctx.tree, "a");
        edit_tree_block_end(&ctx.tree);
        uint64_t c1 = ctx.tree.checksum;
        edit_tui_end(&tui, &ctx);
        CHECK(edit_tui_begin(&tui, NULL, &ctx) == 0);
        edit_tree_block_begin(&ctx.tree, "a");
        edit_tree_block_end(&ctx.tree);
        CHECK(ctx.tree.checksum == c1);
        edit_tui_end(&tui, &ctx);
    }

    // Single-char text input also arrives as keyboard input.
    {
        edit_input_t in = text_input("x");
        edit_ctx_t ctx;
        CHECK(edit_tui_begin(&tui, &in, &ctx) == 0);
        CHECK(ctx.has_text && ctx.text_len == 1);
        CHECK(ctx.has_key && ctx.key == 'X');
        edit_tui_end(&tui, &ctx);
    }
    // Multi-char text does not.
    {
        edit_input_t in = text_input("xy");
        edit_ctx_t ctx;
        CHECK(edit_tui_begin(&tui, &in, &ctx) == 0);
        CHECK(ctx.has_text && !ctx.has_key);
        edit_tui_end(&tui, &ctx);
    }

    // Tab moves focus between focusable nodes; wraps around.
    uint64_t id1 = 0;
    uint64_t id2 = 0;
    {
        frame_buttons(&tui, NULL, &id1, &id2);
        settle(&tui);
        CHECK(edit_tui_is_focused(&tui, EDIT_ROOT_ID));
        edit_input_t tab = key_input(EDIT_VK_TAB);
        frame_buttons(&tui, &tab, NULL, NULL);
        // First Tab: root is focusable — moves into b1.
        CHECK(edit_tui_is_focused(&tui, id1));
        settle(&tui);

        // Second Tab: b1 -> b2.
        frame_buttons(&tui, &tab, NULL, NULL);
        CHECK(edit_tui_is_focused(&tui, id2));
        settle(&tui);

        // Shift+Tab: b2 -> b1.
        edit_input_t stab = key_input(EDIT_KBMOD_SHIFT | EDIT_VK_TAB);
        frame_buttons(&tui, &stab, NULL, NULL);
        CHECK(edit_tui_is_focused(&tui, id1));
        settle(&tui);
    }

    // Mouse down + up on a focusable node focuses and clicks it.
    // Every frame rebuilds the same tree (IDs are deterministic).
    // Settle clickme frames first so hover lands on the node.
    uint64_t click_id = 0;
    for (int i = 0; i < 30; ++i) {
        edit_ctx_t ctx;
        CHECK(edit_tui_begin(&tui, NULL, &ctx) == 0);
        edit_tnode_t *b = edit_tree_block_begin(&ctx.tree, "clickme");
        CHECK(b != NULL);
        b->attributes.focusable = true;
        b->intrinsic_size.width = 10;
        b->intrinsic_size.height = 2;
        b->intrinsic_set = true;
        edit_tree_block_end(&ctx.tree);
        click_id = b->id;
        edit_tui_end(&tui, &ctx);
        if (!edit_tui_needs_settling(&tui)) {
            break;
        }
    }
    {
        // Node occupies (0,0)-(10,2); click at (3,1).
        edit_ctx_t ctx;
        edit_input_t down = mouse_input(EDIT_MOUSE_LEFT, 3, 1);
        CHECK(edit_tui_begin(&tui, &down, &ctx) == 0);
        edit_tnode_t *b = edit_tree_block_begin(&ctx.tree, "clickme");
        b->attributes.focusable = true;
        b->intrinsic_size.width = 10;
        b->intrinsic_size.height = 2;
        b->intrinsic_set = true;
        edit_tree_block_end(&ctx.tree);
        CHECK(b->id == click_id);
        CHECK(ctx.mouse_click == 0); // down alone is not a click
        edit_tui_end(&tui, &ctx);
    }
    {
        edit_ctx_t ctx;
        edit_input_t up = mouse_input(EDIT_MOUSE_NONE, 3, 1);
        CHECK(edit_tui_begin(&tui, &up, &ctx) == 0);
        edit_tnode_t *b = edit_tree_block_begin(&ctx.tree, "clickme");
        b->attributes.focusable = true;
        b->intrinsic_size.width = 10;
        b->intrinsic_size.height = 2;
        b->intrinsic_set = true;
        edit_tree_block_end(&ctx.tree);
        CHECK(ctx.mouse_click == 1);
        CHECK(edit_tui_is_focused(&tui, click_id));
        edit_tui_end(&tui, &ctx);
    }

    // Clipboard set/bump generation.
    {
        size_t n = 0;
        CHECK(edit_tui_clipboard(&tui, &n) == NULL && n == 0);
        CHECK(edit_tui_clipboard_gen(&tui) == 0);
        edit_tui_set_clipboard(&tui, (const uint8_t *)"hi", 2);
        CHECK(edit_tui_clipboard_gen(&tui) == 1);
        const uint8_t *cb = edit_tui_clipboard(&tui, &n);
        CHECK(n == 2 && memcmp(cb, "hi", 2) == 0);
        edit_tui_set_clipboard(&tui, NULL, 0); // empty: ignored
        CHECK(edit_tui_clipboard_gen(&tui) == 1);
    }

    // Palette passthroughs + render smoke (settle clickme frames first).
    {
        for (int i = 0; i < 30 && edit_tui_needs_settling(&tui); ++i) {
            edit_ctx_t ctx;
            CHECK(edit_tui_begin(&tui, NULL, &ctx) == 0);
            edit_tnode_t *b = edit_tree_block_begin(&ctx.tree, "clickme");
            b->attributes.focusable = true;
            b->intrinsic_size.width = 10;
            b->intrinsic_size.height = 2;
            b->intrinsic_set = true;
            edit_tree_block_end(&ctx.tree);
            edit_tui_end(&tui, &ctx);
        }
        CHECK(edit_tui_indexed(&tui, EDIT_FB_RED) != 0);
        CHECK(edit_tui_contrasted(&tui, 0xFF808080) != 0);
        const char *out = NULL;
        size_t n = edit_tui_render(&tui, &out);
        (void)n;
        (void)out;
        CHECK(edit_tui_read_timeout(&tui) == EDIT_TUI_FOREVER);
        CHECK(!edit_tui_needs_settling(&tui));
    }

    // Steal focus + NULL safety.
    {
        edit_ctx_t ctx;
        CHECK(edit_tui_begin(&tui, NULL, &ctx) == 0);
        edit_tnode_t *b = edit_tree_block_begin(&ctx.tree, "focusme");
        CHECK(b != NULL);
        edit_tui_steal_focus(&tui, b);
        CHECK(edit_tui_is_focused(&tui, b->id));
        edit_tui_steal_focus(&tui, b); // already focused: no-op
        edit_tree_block_end(&ctx.tree);
        edit_tui_end(&tui, &ctx);
        CHECK(edit_tui_init(NULL) != 0);
        edit_tui_destroy(NULL);
        CHECK(edit_tui_begin(NULL, NULL, NULL) != 0);
        edit_tui_end(NULL, NULL);
        CHECK(edit_tui_render(NULL, NULL) == 0);
        CHECK(edit_tui_read_timeout(NULL) == EDIT_TUI_FOREVER);
        edit_tui_steal_focus(NULL, NULL);
        CHECK(!edit_tui_is_focused(NULL, 0));
    }

    edit_tui_destroy(&tui);
    printf("test_tuiframe: %d checks passed\n", checks);
    return 0;
}
