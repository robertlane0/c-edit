#include "edit/tui.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "edit/fb.h"
#include "edit/input.h"
#include "edit/measure.h"
#include "edit/uitext.h"
#include "tbuf_priv.h"

// Arena text/chunk appends (frame lifetime, no destructors needed).
static bool text_append(edit_ctx_t *ctx, edit_tnode_t *node, const char *text, size_t len) {
    if (len == 0) {
        return true;
    }
    if (text == NULL) {
        return false;
    }
    edit_arena_t *arena = ctx->tree.arena;
    if (node->text_len > (size_t)-1 - len) {
        return false;
    }
    size_t need = node->text_len + len;
    if (need > node->text_cap) {
        size_t grown = node->text_cap != 0 ? node->text_cap : 32;
        while (grown < need) {
            if (grown > (size_t)-1 / 2) {
                grown = need;
                break;
            }
            grown *= 2;
        }
        void *mem = NULL;
        if (node->text_ptr == NULL) {
            if (!edit_arena_alloc(arena, grown, 1, &mem)) {
                return false;
            }
        } else {
            if (!edit_arena_grow(arena, (void *)node->text_ptr, node->text_cap, grown, 1, &mem)) {
                return false;
            }
        }
        node->text_ptr = (const char *)mem;
        node->text_cap = grown;
    }
    memcpy((char *)node->text_ptr + node->text_len, text, len);
    node->text_len = need;
    return true;
}

static bool chunk_push(edit_ctx_t *ctx, edit_tnode_t *node, size_t offset, uint32_t fg,
                       uint8_t attr) {
    edit_arena_t *arena = ctx->tree.arena;
    if (node->text_nchunks == node->text_chunkcap) {
        size_t grown = node->text_chunkcap != 0 ? node->text_chunkcap * 2 : 4;
        void *mem = NULL;
        if (node->text_chunks == NULL) {
            if (!edit_arena_alloc(arena, grown * sizeof(edit_text_chunk_t),
                                  _Alignof(edit_text_chunk_t), &mem)) {
                return false;
            }
        } else if (!edit_arena_grow(
                       arena, node->text_chunks, node->text_chunkcap * sizeof(edit_text_chunk_t),
                       grown * sizeof(edit_text_chunk_t), _Alignof(edit_text_chunk_t), &mem)) {
            return false;
        }
        node->text_chunks = (edit_text_chunk_t *)mem;
        node->text_chunkcap = grown;
    }
    node->text_chunks[node->text_nchunks].offset = offset;
    node->text_chunks[node->text_nchunks].fg = fg;
    node->text_chunks[node->text_nchunks].attr = attr;
    node->text_nchunks += 1;
    return true;
}

void edit_ctx_block_begin(edit_ctx_t *ctx, const char *classname) {
    if (ctx == NULL) {
        return;
    }
    if (ctx->has_mixin) {
        edit_tree_id_mixin(&ctx->tree, ctx->id_mixin);
        ctx->has_mixin = false;
    }
    edit_tree_block_begin(&ctx->tree, classname == NULL ? "" : classname);
}

void edit_ctx_block_end(edit_ctx_t *ctx) {
    if (ctx == NULL) {
        return;
    }
    edit_tree_block_end(&ctx->tree);
}

void edit_ctx_id_mixin(edit_ctx_t *ctx, uint64_t id) {
    if (ctx == NULL) {
        return;
    }
    ctx->id_mixin = id;
    ctx->has_mixin = true;
}

static edit_tnode_t *last_node(const edit_ctx_t *ctx) {
    return (ctx == NULL) ? NULL : ctx->tree.last_node;
}

void edit_ctx_attr_focus_well(edit_ctx_t *ctx) {
    edit_tnode_t *n = last_node(ctx);
    if (n != NULL) {
        n->attributes.focus_well = true;
    }
}

void edit_ctx_attr_intrinsic_size(edit_ctx_t *ctx, edit_size_t size) {
    edit_tnode_t *n = last_node(ctx);
    if (n != NULL) {
        n->intrinsic_size = size;
        n->intrinsic_set = true;
    }
}

void edit_ctx_attr_float(edit_ctx_t *ctx, int anchor, float gravity_x, float gravity_y,
                         float offset_x, float offset_y) {
    if (ctx == NULL || ctx->tree.last_node == NULL) {
        return;
    }
    edit_tnode_t *last = ctx->tree.last_node;
    edit_tnode_t *anchor_node = NULL;
    if (anchor == 0) {
        anchor_node = (last->sib_prev != NULL) ? last->sib_prev : last->parent;
    } else if (anchor == 1) {
        anchor_node = last->parent;
    }
    edit_tree_to_root(&ctx->tree, last, anchor_node);
    last->attributes.focus_well = true;
    last->attributes.float_attr.has_float = true;
    last->attributes.float_attr.gravity_x = gravity_x < 0.0f   ? 0.0f
                                            : gravity_x > 1.0f ? 1.0f
                                                               : gravity_x;
    last->attributes.float_attr.gravity_y = gravity_y < 0.0f   ? 0.0f
                                            : gravity_y > 1.0f ? 1.0f
                                                               : gravity_y;
    last->attributes.float_attr.offset_x = offset_x;
    last->attributes.float_attr.offset_y = offset_y;
    last->attributes.bg = ctx->tui->floater_bg;
    last->attributes.fg = ctx->tui->floater_fg;
}

void edit_ctx_attr_border(edit_ctx_t *ctx) {
    edit_tnode_t *n = last_node(ctx);
    if (n != NULL) {
        n->attributes.bordered = true;
    }
}

void edit_ctx_attr_position(edit_ctx_t *ctx, int position) {
    edit_tnode_t *n = last_node(ctx);
    if (n != NULL && position >= 0 && position <= 3) {
        n->attributes.position = (edit_position_t)position;
    }
}

void edit_ctx_attr_padding(edit_ctx_t *ctx, edit_rect_t padding) {
    edit_tnode_t *n = last_node(ctx);
    if (n != NULL) {
        n->attributes.padding.left = padding.left < 0 ? 0 : padding.left;
        n->attributes.padding.top = padding.top < 0 ? 0 : padding.top;
        n->attributes.padding.right = padding.right < 0 ? 0 : padding.right;
        n->attributes.padding.bottom = padding.bottom < 0 ? 0 : padding.bottom;
    }
}

void edit_ctx_attr_bg(edit_ctx_t *ctx, uint32_t bg) {
    edit_tnode_t *n = last_node(ctx);
    if (n != NULL) {
        n->attributes.bg = bg;
    }
}

void edit_ctx_attr_fg(edit_ctx_t *ctx, uint32_t fg) {
    edit_tnode_t *n = last_node(ctx);
    if (n != NULL) {
        n->attributes.fg = fg;
    }
}

void edit_ctx_attr_reverse(edit_ctx_t *ctx) {
    edit_tnode_t *n = last_node(ctx);
    if (n != NULL) {
        n->attributes.reverse = true;
    }
}

bool edit_ctx_consume_shortcut(edit_ctx_t *ctx, uint32_t shortcut) {
    if (ctx == NULL || ctx->consumed || !ctx->has_key || ctx->key != shortcut) {
        return false;
    }
    ctx->consumed = true;
    return true;
}

bool edit_ctx_keyboard_input(const edit_ctx_t *ctx, uint32_t *out_key) {
    if (ctx == NULL || ctx->consumed || !ctx->has_key) {
        return false;
    }
    if (out_key != NULL) {
        *out_key = ctx->key;
    }
    return true;
}

void edit_ctx_set_consumed(edit_ctx_t *ctx) {
    if (ctx == NULL) {
        return;
    }
    assert(!ctx->consumed);
    ctx->consumed = true;
}

bool edit_ctx_was_mouse_down(edit_ctx_t *ctx) {
    if (ctx == NULL || ctx->tree.last_node == NULL) {
        return false;
    }
    uint64_t id = ctx->tree.last_node->id;
    edit_tui_t *t = ctx->tui;
    return t->mouse_down_len > 0 && t->mouse_down_path[t->mouse_down_len - 1] == id;
}

bool edit_ctx_contains_mouse_down(edit_ctx_t *ctx) {
    if (ctx == NULL || ctx->tree.last_node == NULL) {
        return false;
    }
    const edit_tnode_t *node = ctx->tree.last_node;
    const edit_tui_t *t = ctx->tui;
    return node->depth < t->mouse_down_len && t->mouse_down_path[node->depth] == node->id;
}

bool edit_ctx_is_focused(edit_ctx_t *ctx) {
    if (ctx == NULL || ctx->tree.last_node == NULL) {
        return false;
    }
    return edit_tui_is_focused(ctx->tui, ctx->tree.last_node->id);
}

bool edit_ctx_contains_focus(edit_ctx_t *ctx) {
    if (ctx == NULL || ctx->tree.last_node == NULL) {
        return false;
    }
    const edit_tnode_t *node = ctx->tree.last_node;
    const edit_tui_t *t = ctx->tui;
    return node->depth < t->focus_len && t->focus_path[node->depth] == node->id;
}

void edit_ctx_focus_on_first_present(edit_ctx_t *ctx) {
    if (ctx == NULL || ctx->tree.last_node == NULL) {
        return;
    }
    edit_tnode_t *node = ctx->tree.last_node;
    node->attributes.focusable = true;
    // Steal if this id was absent in the previous frame.
    if (edit_nodemap_get(&ctx->tui->prev_map, node->id) == NULL) {
        edit_tui_steal_focus(ctx->tui, node);
    }
}

void edit_ctx_steal_focus(edit_ctx_t *ctx) {
    if (ctx == NULL || ctx->tree.last_node == NULL) {
        return;
    }
    edit_tui_steal_focus(ctx->tui, ctx->tree.last_node);
}

void edit_ctx_inherit_focus(edit_ctx_t *ctx) {
    if (ctx == NULL || ctx->tree.last_node == NULL || ctx->tree.last_node->parent == NULL) {
        return;
    }
    edit_tnode_t *node = ctx->tree.last_node;
    edit_tnode_t *parent = node->parent;
    node->attributes.focusable = true;
    parent->attributes.focusable = true;
    if (edit_tui_is_focused(ctx->tui, parent->id)) {
        ctx->needs_settling = true;
        // Push our id (focus path grows; may contain duplicates like Rust).
        edit_tui_t *t = ctx->tui;
        if (t->focus_len < (size_t)-1) {
            // Reuse the path helper via a temporary node walk.
            size_t cap = t->focus_cap;
            if (t->focus_len == cap) {
                size_t grown = cap != 0 ? cap * 2 : 16;
                uint64_t *nb = (uint64_t *)realloc(t->focus_path, grown * sizeof *nb);
                if (nb == NULL) {
                    return;
                }
                t->focus_path = nb;
                t->focus_cap = grown;
            }
            t->focus_path[t->focus_len++] = node->id;
        }
    }
}

void edit_ctx_modal_begin(edit_ctx_t *ctx, const char *classname, const char *title) {
    if (ctx == NULL) {
        return;
    }
    edit_ctx_block_begin(ctx, classname);
    // Float over the whole viewport (Root anchor).
    edit_ctx_attr_float(ctx, 2, 0.0f, 0.0f, 0.0f, 0.0f);
    edit_rect_t vp = {0, 0, ctx->tui->size.width, ctx->tui->size.height};
    edit_ctx_attr_intrinsic_size(ctx, (edit_size_t){vp.right, vp.bottom});
    uint32_t dim = edit_tui_indexed_alpha(ctx->tui, EDIT_FB_BACKGROUND, 1, 2);
    edit_ctx_attr_bg(ctx, dim);
    edit_ctx_attr_fg(ctx, dim);
    edit_ctx_attr_focus_well(ctx);
    // Centered bordered window.
    edit_ctx_block_begin(ctx, "window");
    float cx = (float)ctx->tui->size.width * 0.5f;
    float cy = (float)ctx->tui->size.height * 0.5f;
    edit_ctx_attr_float(ctx, 0, 0.5f, 0.5f, cx, cy);
    edit_ctx_attr_border(ctx);
    edit_ctx_attr_bg(ctx, ctx->tui->modal_bg);
    edit_ctx_attr_fg(ctx, ctx->tui->modal_fg);
    edit_ctx_inherit_focus(ctx);
    edit_ctx_focus_on_first_present(ctx);
    edit_tnode_t *node = ctx->tree.last_node;
    if (node != NULL) {
        node->content_kind = 4;
        node->modal_title = NULL;
        if (title != NULL && title[0] != '\0') {
            size_t n = strlen(title) + 3; // " title "
            void *mem = NULL;
            if (edit_arena_alloc(ctx->tree.arena, n, 1, &mem)) {
                char *buf = (char *)mem;
                buf[0] = ' ';
                memcpy(buf + 1, title, n - 3);
                buf[n - 2] = ' ';
                buf[n - 1] = '\0';
                node->modal_title = buf;
            }
        }
    }
    ctx->last_modal = node;
}

bool edit_ctx_modal_end(edit_ctx_t *ctx) {
    if (ctx == NULL) {
        return false;
    }
    edit_ctx_block_end(ctx);
    edit_ctx_block_end(ctx);
    if (!edit_ctx_contains_focus(ctx)) {
        return false;
    }
    bool exit = !ctx->consumed && ctx->has_key && ctx->key == (uint32_t)EDIT_VK_ESCAPE;
    ctx->consumed = true;
    return exit;
}

void edit_ctx_table_begin(edit_ctx_t *ctx, const char *classname) {
    if (ctx == NULL) {
        return;
    }
    edit_ctx_block_begin(ctx, classname);
    edit_tnode_t *node = ctx->tree.last_node;
    if (node != NULL) {
        node->content_kind = 1;
    }
}

bool edit_ctx_table_set_columns(edit_ctx_t *ctx, const int32_t *columns, size_t ncols) {
    if (ctx == NULL || ctx->tree.last_node == NULL) {
        return false;
    }
    edit_tnode_t *node = ctx->tree.last_node;
    if (node->content_kind != 1) {
        assert(false && "not a table");
        return false;
    }
    // Clear + extend (mirrors Vec semantics; layout regrows as needed).
    node->table_ncols = 0;
    for (size_t i = 0; i < ncols; ++i) {
        if (node->table_ncols == node->table_capcols) {
            size_t grown = node->table_capcols != 0 ? node->table_capcols * 2 : 4;
            void *mem = NULL;
            if (node->table_columns == NULL) {
                if (!edit_arena_alloc(ctx->tree.arena, grown * sizeof(int32_t), _Alignof(int32_t),
                                      &mem)) {
                    return false;
                }
            } else if (!edit_arena_grow(ctx->tree.arena, node->table_columns,
                                        node->table_capcols * sizeof(int32_t),
                                        grown * sizeof(int32_t), _Alignof(int32_t), &mem)) {
                return false;
            }
            node->table_columns = (int32_t *)mem;
            node->table_capcols = grown;
        }
        node->table_columns[node->table_ncols++] = columns[i];
    }
    return true;
}

bool edit_ctx_table_set_gap(edit_ctx_t *ctx, edit_size_t gap) {
    if (ctx == NULL || ctx->tree.last_node == NULL) {
        return false;
    }
    edit_tnode_t *node = ctx->tree.last_node;
    if (node->content_kind != 1) {
        assert(false && "not a table");
        return false;
    }
    node->table_gap = gap;
    return true;
}

void edit_ctx_table_next_row(edit_ctx_t *ctx) {
    if (ctx == NULL || ctx->tree.current_node == NULL) {
        return;
    }
    if (ctx->tree.current_node->content_kind != 1) {
        edit_tnode_t *parent = ctx->tree.current_node->parent;
        if (parent == NULL) {
            return;
        }
        assert(parent->content_kind == 1);
        edit_ctx_block_end(ctx);
        edit_ctx_id_mixin(ctx, (uint64_t)parent->child_count);
    }
    edit_ctx_block_begin(ctx, "row");
}

void edit_ctx_table_end(edit_ctx_t *ctx) {
    if (ctx == NULL || ctx->tree.current_node == NULL) {
        return;
    }
    if (ctx->tree.current_node->content_kind != 1) {
        edit_ctx_block_end(ctx);
    }
    edit_ctx_block_end(ctx);
}

void edit_ctx_label(edit_ctx_t *ctx, const char *classname, const char *text) {
    if (ctx == NULL) {
        return;
    }
    edit_ctx_styled_begin(ctx, classname);
    edit_ctx_styled_add(ctx, text == NULL ? "" : text, text == NULL ? 0 : strlen(text));
    edit_ctx_styled_end(ctx);
}

void edit_ctx_styled_begin(edit_ctx_t *ctx, const char *classname) {
    if (ctx == NULL) {
        return;
    }
    edit_ctx_block_begin(ctx, classname);
    edit_tnode_t *node = ctx->tree.last_node;
    if (node != NULL) {
        node->content_kind = 3;
        node->text_ptr = NULL;
        node->text_len = 0;
        node->text_cap = 0;
        node->text_chunks = NULL;
        node->text_nchunks = 0;
        node->text_chunkcap = 0;
        node->text_overflow = EDIT_OVF_CLIP;
    }
}

bool edit_ctx_styled_fg(edit_ctx_t *ctx, uint32_t fg) {
    if (ctx == NULL || ctx->tree.last_node == NULL) {
        return false;
    }
    edit_tnode_t *node = ctx->tree.last_node;
    if (node->content_kind != 3) {
        return false;
    }
    size_t last_off = (size_t)-1;
    uint32_t last_fg = 0;
    uint8_t last_attr = 0;
    if (node->text_nchunks > 0) {
        last_off = node->text_chunks[node->text_nchunks - 1].offset;
        last_fg = node->text_chunks[node->text_nchunks - 1].fg;
        last_attr = node->text_chunks[node->text_nchunks - 1].attr;
    }
    if (last_off != node->text_len && last_fg != fg) {
        return chunk_push(ctx, node, node->text_len, fg, last_attr);
    }
    return true;
}

bool edit_ctx_styled_attr(edit_ctx_t *ctx, uint8_t attr) {
    if (ctx == NULL || ctx->tree.last_node == NULL) {
        return false;
    }
    edit_tnode_t *node = ctx->tree.last_node;
    if (node->content_kind != 3) {
        return false;
    }
    size_t last_off = (size_t)-1;
    uint32_t last_fg = 0;
    uint8_t last_attr = 0;
    if (node->text_nchunks > 0) {
        last_off = node->text_chunks[node->text_nchunks - 1].offset;
        last_fg = node->text_chunks[node->text_nchunks - 1].fg;
        last_attr = node->text_chunks[node->text_nchunks - 1].attr;
    }
    if (last_off != node->text_len && last_attr != attr) {
        return chunk_push(ctx, node, node->text_len, last_fg, attr);
    }
    return true;
}

bool edit_ctx_styled_add(edit_ctx_t *ctx, const char *text, size_t len) {
    if (ctx == NULL || ctx->tree.last_node == NULL) {
        return false;
    }
    edit_tnode_t *node = ctx->tree.last_node;
    if (node->content_kind != 3) {
        return false;
    }
    return text_append(ctx, node, text, len);
}

void edit_ctx_styled_end(edit_ctx_t *ctx) {
    if (ctx == NULL || ctx->tree.last_node == NULL) {
        edit_ctx_block_end(ctx);
        return;
    }
    edit_tnode_t *node = ctx->tree.last_node;
    if (node->content_kind == 3) {
        size_t width = edit_text_measure(node->text_ptr, node->text_len);
        node->intrinsic_size.width = width > INT32_MAX ? INT32_MAX : (int32_t)width;
        node->intrinsic_size.height = 1;
        node->intrinsic_set = true;
    }
    edit_ctx_block_end(ctx);
}

void edit_ctx_set_overflow(edit_ctx_t *ctx, int overflow) {
    if (ctx == NULL || ctx->tree.last_node == NULL) {
        return;
    }
    edit_tnode_t *node = ctx->tree.last_node;
    if (node->content_kind != 3) {
        return;
    }
    if (overflow >= 0 && overflow <= 3) {
        node->text_overflow = (edit_overflow_t)overflow;
    }
}

static bool button_activated(edit_ctx_t *ctx) {
    bool click = ctx->mouse_click != 0 && edit_ctx_contains_mouse_down(ctx);
    bool key = ctx->has_key &&
               (ctx->key == (uint32_t)EDIT_VK_RETURN || ctx->key == (uint32_t)EDIT_VK_SPACE);
    if (!ctx->consumed && (click || key) && edit_ctx_is_focused(ctx)) {
        ctx->consumed = true;
        return true;
    }
    return false;
}

bool edit_ctx_button(edit_ctx_t *ctx, const char *classname, const char *text) {
    if (ctx == NULL) {
        return false;
    }
    edit_ctx_styled_begin(ctx, classname);
    // focusable marker (attr helper operates on last node).
    if (ctx->tree.last_node != NULL) {
        ctx->tree.last_node->attributes.focusable = true;
    }
    if (edit_ctx_is_focused(ctx)) {
        if (ctx->tree.last_node != NULL) {
            ctx->tree.last_node->attributes.reverse = true;
        }
    }
    const char *label = text == NULL ? "" : text;
    edit_ctx_styled_add(ctx, "[", 1);
    edit_ctx_styled_add(ctx, label, strlen(label));
    edit_ctx_styled_add(ctx, "]", 1);
    edit_ctx_styled_end(ctx);
    return button_activated(ctx);
}

bool edit_ctx_checkbox(edit_ctx_t *ctx, const char *classname, const char *text, bool *checked) {
    if (ctx == NULL || checked == NULL) {
        return false;
    }
    edit_ctx_styled_begin(ctx, classname);
    if (ctx->tree.last_node != NULL) {
        ctx->tree.last_node->attributes.focusable = true;
    }
    if (edit_ctx_is_focused(ctx)) {
        if (ctx->tree.last_node != NULL) {
            ctx->tree.last_node->attributes.reverse = true;
        }
    }
    const char *label = text == NULL ? "" : text;
    edit_ctx_styled_add(ctx, *checked ? "[\xE2\x96\xA3 " : "[\xE2\x98\x90 ", 5);
    edit_ctx_styled_add(ctx, label, strlen(label));
    edit_ctx_styled_add(ctx, "]", 1);
    edit_ctx_styled_end(ctx);
    bool activated = button_activated(ctx);
    if (activated) {
        *checked = !*checked;
    }
    return activated;
}

static void textarea_make_visible(edit_tbuf_t *tb, const edit_tnode_t *node, int32_t *scroll_x,
                                  int32_t *scroll_y) {
    int32_t text_width = edit_tbuf_text_width(tb);
    int32_t cursor_x = edit_tbuf_cursor_visual(tb).x;
    if (*scroll_x > cursor_x - 10) {
        *scroll_x = cursor_x - 10;
    }
    if (*scroll_x < cursor_x - text_width + 10) {
        *scroll_x = cursor_x - text_width + 10;
    }
    int32_t viewport_h = node->inner.bottom - node->inner.top;
    int32_t cursor_y = edit_tbuf_cursor_visual(tb).y;
    if (*scroll_y > cursor_y) {
        *scroll_y = cursor_y;
    }
    if (*scroll_y < cursor_y - viewport_h + 1) {
        *scroll_y = cursor_y - viewport_h + 1;
    }
}

static void textarea_adjust(edit_tbuf_t *tb, int32_t xmax, int32_t *scroll_x, int32_t *scroll_y) {
    int32_t cx = edit_tbuf_cursor_visual(tb).x;
    int32_t lim = xmax > cx ? xmax : cx;
    if (*scroll_x > lim - 10) {
        *scroll_x = lim - 10;
    }
    if (*scroll_x < 0) {
        *scroll_x = 0;
    }
    int32_t lines = edit_tbuf_visual_lines(tb);
    if (*scroll_y < 0) {
        *scroll_y = 0;
    }
    if (*scroll_y > lines - 1) {
        *scroll_y = lines - 1;
    }
    if (edit_tbuf_is_wrap(tb)) {
        *scroll_x = 0;
    }
}

// Drag-scroll speed table (mirrors Rust calc()).
static int32_t drag_speed(int32_t mn, int32_t mx, int32_t mouse) {
    int32_t zone = (mx - mn) / 2;
    if (zone > 3) {
        zone = 3;
    }
    int32_t s0 = mn + zone;
    int32_t s1 = mx - zone - 1;
    int32_t a = mouse - s0;
    if (a < -zone) {
        a = -zone;
    }
    if (a > 0) {
        a = 0;
    }
    int32_t b = mouse - s1;
    if (b < 0) {
        b = 0;
    }
    if (b > zone) {
        b = zone;
    }
    int32_t idx = 3 + a + b;
    if (idx < 0) {
        idx = 0;
    }
    if (idx > 6) {
        idx = 6;
    }
    static const int32_t speeds[7] = {-9, -3, -1, 0, 1, 3, 9};
    return speeds[idx];
}

static bool handle_input(edit_ctx_t *ctx, edit_tbuf_t *tb, edit_tnode_t *node,
                         const edit_tnode_t *prev, bool single_line) {
    edit_tui_t *tui = ctx->tui;
    if (ctx->consumed) {
        return false;
    }
    bool visible = false;

    if (tui->mouse_state != EDIT_MOUSE_NONE && prev != NULL && tui->mouse_down_len > 0 &&
        tui->mouse_down_path[tui->mouse_down_len - 1] == prev->id) {
        if (tui->mouse_state == EDIT_MOUSE_SCROLL) {
            node->ta_scroll.x += ctx->scroll_delta.x;
            node->ta_scroll.y += ctx->scroll_delta.y;
            ctx->consumed = true;
        } else if (edit_tui_is_focused(tui, prev->id)) {
            edit_point_t mouse = tui->mouse_pos;
            edit_rect_t inner = prev->inner;
            int32_t margin = edit_tbuf_margin_width(tb);
            edit_rect_t text_rect = {inner.left + margin, inner.top,
                                     inner.right - (single_line ? 0 : 1), inner.bottom};
            edit_rect_t track = {text_rect.right, inner.top, inner.right, inner.bottom};
            edit_point_t pos = {mouse.x - inner.left - margin + node->ta_scroll.x,
                                mouse.y - inner.top + node->ta_scroll.y};
            if (edit_rect_contains(text_rect, tui->mouse_down_pos)) {
                if (tui->mouse_is_drag) {
                    edit_tbuf_selection_update_visual(tb, pos);
                    node->ta_preferred = edit_tbuf_cursor_visual(tb).x;
                    int32_t height = inner.bottom - inner.top;
                    if (height >= 2) {
                        int32_t dx = drag_speed(text_rect.left, text_rect.right, mouse.x);
                        int32_t dy = drag_speed(text_rect.top, text_rect.bottom, mouse.y);
                        node->ta_scroll.x += dx;
                        node->ta_scroll.y += dy;
                        if (dx != 0 || dy != 0) {
                            tui->read_timeout = 25;
                        }
                    }
                } else {
                    if (ctx->mouse_click >= 5) {
                    } else if (ctx->mouse_click == 4) {
                        edit_tbuf_select_all(tb);
                    } else if (ctx->mouse_click == 3) {
                        edit_tbuf_select_line(tb);
                    } else if (ctx->mouse_click == 2) {
                        edit_tbuf_select_word(tb);
                    } else if (tui->mouse_state == EDIT_MOUSE_LEFT) {
                        if (edit_mod_contains(ctx->mouse_mods, EDIT_KBMOD_SHIFT)) {
                            edit_tbuf_selection_update_visual(tb, pos);
                        } else {
                            edit_tbuf_goto_visual(tb, pos);
                        }
                        node->ta_preferred = edit_tbuf_cursor_visual(tb).x;
                        visible = true;
                    } else {
                        return false;
                    }
                }
            } else if (edit_rect_contains(track, tui->mouse_down_pos)) {
                if (tui->mouse_state == EDIT_MOUSE_RELEASE) {
                    node->ta_drag_start = INT32_MIN;
                } else if (tui->mouse_is_drag) {
                    if (node->ta_drag_start == INT32_MIN) {
                        node->ta_drag_start = node->ta_scroll.y;
                    }
                    int32_t scrollable = edit_tbuf_visual_lines(tb) - 1;
                    if (scrollable > 0) {
                        int32_t trackable = track.bottom - track.top - node->ta_thumb;
                        int32_t dy = mouse.y - tui->mouse_down_pos.y;
                        if (trackable > 0) {
                            node->ta_scroll.y = node->ta_drag_start + (dy * scrollable) / trackable;
                        }
                    }
                }
            }
            ctx->consumed = true;
        }
        return visible;
    }

    if (!node->ta_has_focus) {
        return false;
    }

    const uint8_t *write = (const uint8_t *)"";
    size_t write_len = 0;
    bool write_raw = false;

    if (ctx->has_text) {
        write = ctx->text_ptr;
        write_len = ctx->text_len;
        write_raw = ctx->text_bracketed;
        node->ta_preferred = edit_tbuf_cursor_visual(tb).x;
        visible = true;
    } else if (ctx->has_key) {
        uint32_t code = edit_key_code(ctx->key);
        uint32_t mods = edit_key_modifiers(ctx->key);
        visible = true;
        switch (code) {
        case EDIT_VK_BACK:
            edit_tbuf_delete(tb, (mods == EDIT_KBMOD_CTRL) ? EDIT_MOVE_WORD : EDIT_MOVE_GRAPHEME,
                             -1);
            break;
        case EDIT_VK_TAB:
            if (single_line) {
                return false;
            }
            if (mods == EDIT_KBMOD_SHIFT) {
                edit_tbuf_unindent(tb);
            } else {
                write = (const uint8_t *)"\t";
                write_len = 1;
            }
            break;
        case EDIT_VK_RETURN:
            if (single_line) {
                return false;
            }
            write = (const uint8_t *)"\n";
            write_len = 1;
            break;
        case EDIT_VK_ESCAPE:
            if (!edit_tbuf_clear_selection(tb)) {
                if (single_line) {
                    return false;
                }
                visible = false;
            }
            break;
        case EDIT_VK_PRIOR: {
            int32_t height = prev->inner.bottom - prev->inner.top - 1;
            if (edit_tbuf_cursor_visual(tb).y == 0) {
                node->ta_preferred = 0;
            }
            edit_point_t p = {node->ta_preferred, edit_tbuf_cursor_visual(tb).y - height};
            if (mods == EDIT_KBMOD_SHIFT) {
                edit_tbuf_selection_update_visual(tb, p);
            } else {
                edit_tbuf_goto_visual(tb, p);
            }
            break;
        }
        case EDIT_VK_NEXT: {
            int32_t height = prev->inner.bottom - prev->inner.top - 1;
            if (edit_tbuf_cursor_visual(tb).y >= edit_tbuf_visual_lines(tb) - 1) {
                node->ta_preferred = INT32_MAX;
            }
            edit_point_t p = {node->ta_preferred, edit_tbuf_cursor_visual(tb).y + height};
            if (mods == EDIT_KBMOD_SHIFT) {
                edit_tbuf_selection_update_visual(tb, p);
            } else {
                edit_tbuf_goto_visual(tb, p);
            }
            if (node->ta_preferred == INT32_MAX) {
                node->ta_preferred = edit_tbuf_cursor_visual(tb).x;
            }
            break;
        }
        case EDIT_VK_END: {
            edit_point_t before = edit_tbuf_cursor_logical(tb);
            edit_point_t dest;
            if (edit_mod_contains(mods, EDIT_KBMOD_CTRL)) {
                dest.x = INT32_MAX;
                dest.y = INT32_MAX;
            } else {
                dest.x = INT32_MAX;
                dest.y = edit_tbuf_cursor_visual(tb).y;
            }
            if (edit_mod_contains(mods, EDIT_KBMOD_SHIFT)) {
                edit_tbuf_selection_update_visual(tb, dest);
            } else {
                edit_tbuf_goto_visual(tb, dest);
            }
            if (!edit_mod_contains(mods, EDIT_KBMOD_CTRL)) {
                edit_point_t after = edit_tbuf_cursor_logical(tb);
                if (edit_tbuf_is_wrap(tb) && after.x == before.x && after.y == before.y) {
                    edit_point_t p = {INT32_MAX, edit_tbuf_cursor_logical(tb).y};
                    if (mods == EDIT_KBMOD_SHIFT) {
                        edit_tbuf_selection_update_logical(tb, p);
                    } else {
                        edit_tbuf_goto_logical(tb, p);
                    }
                }
            }
            break;
        }
        case EDIT_VK_HOME: {
            edit_point_t before = edit_tbuf_cursor_logical(tb);
            edit_point_t dest;
            if (edit_mod_contains(mods, EDIT_KBMOD_CTRL)) {
                dest.x = 0;
                dest.y = 0;
            } else {
                dest.x = 0;
                dest.y = edit_tbuf_cursor_visual(tb).y;
            }
            if (edit_mod_contains(mods, EDIT_KBMOD_SHIFT)) {
                edit_tbuf_selection_update_visual(tb, dest);
            } else {
                edit_tbuf_goto_visual(tb, dest);
            }
            if (!edit_mod_contains(mods, EDIT_KBMOD_CTRL)) {
                edit_point_t after = edit_tbuf_cursor_logical(tb);
                if (edit_tbuf_is_wrap(tb) && after.x == before.x && after.y == before.y) {
                    edit_point_t p = {0, edit_tbuf_cursor_logical(tb).y};
                    if (mods == EDIT_KBMOD_SHIFT) {
                        edit_tbuf_selection_update_logical(tb, p);
                    } else {
                        edit_tbuf_goto_logical(tb, p);
                    }
                    after = edit_tbuf_cursor_logical(tb);
                }
                edit_point_t indent = edit_tbuf_indent_end(tb);
                if (after.x == 0 &&
                    (before.y > indent.y || (before.y == indent.y && before.x > indent.x))) {
                    if (edit_mod_contains(mods, EDIT_KBMOD_SHIFT)) {
                        edit_tbuf_selection_update_logical(tb, indent);
                    } else {
                        edit_tbuf_goto_logical(tb, indent);
                    }
                }
            }
            break;
        }
        case EDIT_VK_LEFT: {
            bool word = edit_mod_contains(mods, EDIT_KBMOD_CTRL);
            if (edit_mod_contains(mods, EDIT_KBMOD_SHIFT)) {
                edit_tbuf_selection_update_delta(tb, word ? EDIT_MOVE_WORD : EDIT_MOVE_GRAPHEME,
                                                 -1);
            } else {
                edit_cursor_t b;
                edit_cursor_t e;
                if (edit_tbuf_selection_range(tb, &b, &e)) {
                    tbuf_set_cursor_internal(tb, b);
                    tb->hist_last = 0;
                    edit_point_t z = {0, 0};
                    tbuf_set_selection(tb, false, z, z);
                } else {
                    edit_tbuf_move_delta(tb, word ? EDIT_MOVE_WORD : EDIT_MOVE_GRAPHEME, -1);
                }
            }
            break;
        }
        case EDIT_VK_UP:
            if (mods == EDIT_KBMOD_NONE) {
                int32_t x = node->ta_preferred;
                int32_t y = edit_tbuf_cursor_visual(tb).y - 1;
                edit_cursor_t b;
                edit_cursor_t e;
                if (edit_tbuf_selection_range(tb, &b, &e)) {
                    x = b.visual.x;
                    y = b.visual.y - 1;
                    node->ta_preferred = x;
                }
                if (y < 0) {
                    x = 0;
                    node->ta_preferred = 0;
                }
                edit_tbuf_goto_visual(tb, (edit_point_t){x, y});
            } else if (mods == EDIT_KBMOD_CTRL) {
                node->ta_scroll.y -= 1;
                visible = false;
            } else if (mods == EDIT_KBMOD_SHIFT) {
                if (edit_tbuf_cursor_visual(tb).y == 0) {
                    node->ta_preferred = 0;
                }
                edit_point_t p = {node->ta_preferred, edit_tbuf_cursor_visual(tb).y - 1};
                edit_tbuf_selection_update_visual(tb, p);
            } else if (mods == (EDIT_KBMOD_CTRL | EDIT_KBMOD_ALT)) {
                // TODO: multi-cursor above (unimplemented in Rust).
            } else {
                return false;
            }
            break;
        case EDIT_VK_RIGHT: {
            bool word = edit_mod_contains(mods, EDIT_KBMOD_CTRL);
            if (edit_mod_contains(mods, EDIT_KBMOD_SHIFT)) {
                edit_tbuf_selection_update_delta(tb, word ? EDIT_MOVE_WORD : EDIT_MOVE_GRAPHEME, 1);
            } else {
                edit_cursor_t b;
                edit_cursor_t e;
                if (edit_tbuf_selection_range(tb, &b, &e)) {
                    tbuf_set_cursor_internal(tb, e);
                    tb->hist_last = 0;
                    edit_point_t z = {0, 0};
                    tbuf_set_selection(tb, false, z, z);
                } else {
                    edit_tbuf_move_delta(tb, word ? EDIT_MOVE_WORD : EDIT_MOVE_GRAPHEME, 1);
                }
            }
            break;
        }
        case EDIT_VK_DOWN:
            if (mods == EDIT_KBMOD_NONE) {
                int32_t x = node->ta_preferred;
                int32_t y = edit_tbuf_cursor_visual(tb).y + 1;
                edit_cursor_t b;
                edit_cursor_t e;
                if (edit_tbuf_selection_range(tb, &b, &e)) {
                    x = e.visual.x;
                    y = e.visual.y + 1;
                    node->ta_preferred = x;
                }
                if (y >= edit_tbuf_visual_lines(tb)) {
                    x = INT32_MAX;
                }
                edit_tbuf_goto_visual(tb, (edit_point_t){x, y});
                if (x == INT32_MAX) {
                    node->ta_preferred = edit_tbuf_cursor_visual(tb).x;
                }
            } else if (mods == EDIT_KBMOD_CTRL) {
                node->ta_scroll.y += 1;
                visible = false;
            } else if (mods == EDIT_KBMOD_SHIFT) {
                if (edit_tbuf_cursor_visual(tb).y >= edit_tbuf_visual_lines(tb) - 1) {
                    node->ta_preferred = INT32_MAX;
                }
                edit_point_t p = {node->ta_preferred, edit_tbuf_cursor_visual(tb).y + 1};
                edit_tbuf_selection_update_visual(tb, p);
                if (node->ta_preferred == INT32_MAX) {
                    node->ta_preferred = edit_tbuf_cursor_visual(tb).x;
                }
            } else if (mods == (EDIT_KBMOD_CTRL | EDIT_KBMOD_ALT)) {
                // TODO: multi-cursor below (unimplemented in Rust).
            } else {
                return false;
            }
            break;
        case EDIT_VK_INSERT:
            if (mods == EDIT_KBMOD_SHIFT) {
                size_t n = 0;
                const uint8_t *cb = edit_tui_clipboard(tui, &n);
                write = cb;
                write_len = n;
                write_raw = true;
            } else if (mods == EDIT_KBMOD_CTRL) {
                size_t n = 0;
                uint8_t *sel = edit_tbuf_extract_selection(tb, false, &n);
                if (sel != NULL) {
                    edit_tui_set_clipboard(tui, sel, n);
                    free(sel);
                }
            } else {
                edit_tbuf_set_overtype(tb, !edit_tbuf_is_overtype(tb));
            }
            break;
        case EDIT_VK_DELETE:
            if (mods == EDIT_KBMOD_SHIFT) {
                size_t n = 0;
                uint8_t *sel = edit_tbuf_extract_selection(tb, true, &n);
                if (sel != NULL) {
                    edit_tui_set_clipboard(tui, sel, n);
                    free(sel);
                }
            } else if (mods == EDIT_KBMOD_CTRL) {
                edit_tbuf_delete(tb, EDIT_MOVE_WORD, 1);
            } else {
                edit_tbuf_delete(tb, EDIT_MOVE_GRAPHEME, 1);
            }
            break;
        case 'A':
            if (mods == EDIT_KBMOD_CTRL) {
                edit_tbuf_select_all(tb);
            } else {
                return false;
            }
            break;
        case 'H':
            if (mods == EDIT_KBMOD_CTRL) {
                edit_tbuf_delete(tb, EDIT_MOVE_WORD, -1);
            } else {
                return false;
            }
            break;
        case 'X':
            if (mods == EDIT_KBMOD_CTRL) {
                size_t n = 0;
                uint8_t *sel = edit_tbuf_extract_selection(tb, true, &n);
                if (sel != NULL) {
                    edit_tui_set_clipboard(tui, sel, n);
                    free(sel);
                }
            } else {
                return false;
            }
            break;
        case 'C':
            if (mods == EDIT_KBMOD_CTRL) {
                size_t n = 0;
                uint8_t *sel = edit_tbuf_extract_selection(tb, false, &n);
                if (sel != NULL) {
                    edit_tui_set_clipboard(tui, sel, n);
                    free(sel);
                }
            } else {
                return false;
            }
            break;
        case 'V':
            if (mods == EDIT_KBMOD_CTRL) {
                size_t n = 0;
                write = edit_tui_clipboard(tui, &n);
                write_len = n;
                write_raw = true;
            } else {
                return false;
            }
            break;
        case 'Y':
            if (mods == EDIT_KBMOD_CTRL) {
                edit_tbuf_redo(tb);
            } else {
                return false;
            }
            break;
        case 'Z':
            if (mods == EDIT_KBMOD_CTRL) {
                edit_tbuf_undo(tb);
            } else if (mods == (EDIT_KBMOD_CTRL | EDIT_KBMOD_SHIFT)) {
                edit_tbuf_redo(tb);
            } else if (mods == EDIT_KBMOD_ALT) {
                edit_tbuf_set_wrap(tb, !edit_tbuf_is_wrap(tb));
            } else {
                return false;
            }
            break;
        default:
            return false;
        }

        if (code != EDIT_VK_PRIOR && code != EDIT_VK_NEXT && code != EDIT_VK_UP &&
            code != EDIT_VK_DOWN) {
            node->ta_preferred = edit_tbuf_cursor_visual(tb).x;
        }
    } else {
        return false;
    }

    if (single_line && write_len > 0) {
        size_t end = 0;
        int32_t line = 0;
        edit_newlines_forward(write, write_len, 0, 0, 1, &end, &line);
        write_len = edit_strip_newline(write, end);
    }
    if (write_len > 0) {
        edit_tbuf_write(tb, write, write_len, write_raw);
    }

    ctx->consumed = true;
    return visible;
}

static bool textarea_internal(edit_ctx_t *ctx, const char *classname, bool single_line,
                              edit_doc_t *edit_doc, edit_shared_tbuf_t *shared, bool *out_dirty) {
    if (out_dirty != NULL) {
        *out_dirty = false;
    }
    if (ctx == NULL) {
        return false;
    }
    edit_ctx_block_begin(ctx, classname);
    edit_ctx_block_end(ctx);
    edit_tnode_t *node = ctx->tree.last_node;
    if (node == NULL) {
        return false;
    }

    // Cached editor by node id (editline owns one; textarea borrows shared).
    edit_tbuf_t *tb = NULL;
    if (single_line) {
        edit_tui_t *tui = ctx->tui;
        edit_shared_tbuf_t *cached = NULL;
        for (size_t i = 0; i < tui->tbuf_cache_len; ++i) {
            if (tui->tbuf_cache[i].node_id == node->id) {
                cached = tui->tbuf_cache[i].editor;
                tui->tbuf_cache[i].seen = true;
                break;
            }
        }
        if (cached == NULL) {
            if (edit_shared_tbuf_create(&cached, true) != 0) {
                return false;
            }
            if (tui->tbuf_cache_len == tui->tbuf_cache_cap) {
                size_t grown = tui->tbuf_cache_cap != 0 ? tui->tbuf_cache_cap * 2 : 16;
                void *nb = realloc(tui->tbuf_cache, grown * sizeof(*tui->tbuf_cache));
                if (nb == NULL) {
                    edit_shared_release(cached);
                    return false;
                }
                tui->tbuf_cache = nb;
                tui->tbuf_cache_cap = grown;
            }
            tui->tbuf_cache[tui->tbuf_cache_len].node_id = node->id;
            tui->tbuf_cache[tui->tbuf_cache_len].editor = cached;
            tui->tbuf_cache[tui->tbuf_cache_len].seen = true;
            tui->tbuf_cache_len += 1;
        }
        tb = &cached->tbuf;
    } else {
        if (shared == NULL) {
            return false;
        }
        tb = &shared->tbuf;
    }

    node->content_kind = 5;
    node->ta_buffer = single_line ? NULL : (void *)shared;
    // Retain scroll etc. across frames via prev node below; init defaults.
    node->ta_scroll.x = 0;
    node->ta_scroll.y = 0;
    node->ta_drag_start = INT32_MIN;
    node->ta_xmax = 0;
    node->ta_thumb = 0;
    node->ta_preferred = 0;
    node->ta_single_line = single_line;
    node->ta_has_focus = edit_tui_is_focused(ctx->tui, node->id);

    if (single_line && edit_doc != NULL) {
        // Sync the field buffer from the caller document.
        edit_tbuf_copy_from(tb, edit_doc);
    }

    edit_tnode_t *prev = edit_nodemap_get(&ctx->tui->prev_map, node->id);
    if (prev != NULL && prev->content_kind == 5) {
        node->ta_scroll = prev->ta_scroll;
        node->ta_drag_start = prev->ta_drag_start;
        node->ta_xmax = prev->ta_xmax;
        node->ta_thumb = prev->ta_thumb;
        node->ta_preferred = prev->ta_preferred;

        int32_t text_width = node->ta_single_line ? INT32_MAX : 0;
        (void)text_width;
        bool make_visible = false;
        // Width sync + input handling (mirrors Rust ordering).
        {
            // text_width = prev inner width (-1 scrollbar when multiline).
            int32_t width = prev->inner.right - prev->inner.left;
            if (!single_line) {
                width -= 1;
            }
            if (edit_tbuf_set_width(tb, width)) {
                make_visible = true;
            }
        }
        make_visible |= handle_input(ctx, tb, node, prev, single_line);
        if (make_visible) {
            int32_t sx = node->ta_scroll.x;
            int32_t sy = node->ta_scroll.y;
            textarea_make_visible(tb, prev, &sx, &sy);
            node->ta_scroll.x = sx;
            node->ta_scroll.y = sy;
        }
    }

    bool dirty = edit_tbuf_is_dirty(tb);
    if (dirty && single_line && edit_doc != NULL) {
        edit_tbuf_save_to(tb, edit_doc);
    }

    {
        int32_t sx = node->ta_scroll.x;
        int32_t sy = node->ta_scroll.y;
        textarea_adjust(tb, node->ta_xmax, &sx, &sy);
        node->ta_scroll.x = sx;
        node->ta_scroll.y = sy;
    }

    if (single_line) {
        node->attributes.fg = edit_tui_indexed(ctx->tui, EDIT_FB_FOREGROUND);
        node->attributes.bg = edit_tui_indexed(ctx->tui, EDIT_FB_BACKGROUND);
        if (!node->ta_has_focus) {
            node->attributes.fg = edit_tui_contrasted(ctx->tui, node->attributes.bg);
            node->attributes.bg = edit_tui_indexed_alpha(ctx->tui, EDIT_FB_BACKGROUND, 1, 2);
        }
    }

    node->attributes.focusable = true;
    node->intrinsic_size.height = edit_tbuf_visual_lines(tb);
    node->intrinsic_set = true;

    if (out_dirty != NULL) {
        *out_dirty = dirty;
    }
    return dirty;
}

bool edit_ctx_editline(edit_ctx_t *ctx, const char *classname, edit_doc_t *doc) {
    if (ctx == NULL || doc == NULL) {
        return false;
    }
    bool dirty = false;
    textarea_internal(ctx, classname == NULL ? "" : classname, true, doc, NULL, &dirty);
    return dirty;
}

void edit_ctx_textarea(edit_ctx_t *ctx, const char *classname, edit_shared_tbuf_t *shared) {
    if (ctx == NULL) {
        return;
    }
    textarea_internal(ctx, classname == NULL ? "" : classname, false, NULL, shared, NULL);
}

void edit_ctx_list_begin(edit_ctx_t *ctx, const char *classname) {
    if (ctx == NULL) {
        return;
    }
    edit_ctx_block_begin(ctx, classname);
    edit_tnode_t *node = ctx->tree.last_node;
    if (node == NULL) {
        return;
    }
    uint64_t selected = 0;
    edit_tnode_t *prev = edit_nodemap_get(&ctx->tui->prev_map, node->id);
    if (prev != NULL && prev->content_kind == 6) {
        selected = prev->list_selected;
    }
    node->attributes.focusable = true;
    node->attributes.focus_void = true;
    node->content_kind = 6;
    node->list_selected = selected;
    node->list_selected_node = NULL;
}

void edit_ctx_styled_list_item_begin(edit_ctx_t *ctx) {
    if (ctx == NULL || ctx->tree.current_node == NULL) {
        return;
    }
    edit_ctx_id_mixin(ctx, (uint64_t)ctx->tree.current_node->child_count);
    edit_ctx_styled_begin(ctx, "item");
    edit_ctx_styled_add(ctx, "  ", 2);
    if (ctx->tree.last_node != NULL) {
        ctx->tree.last_node->attributes.focusable = true;
    }
}

int edit_ctx_styled_list_item_end(edit_ctx_t *ctx, bool select) {
    if (ctx == NULL) {
        return 0;
    }
    edit_ctx_styled_end(ctx);
    edit_tnode_t *list = ctx->tree.current_node;
    if (list == NULL || list->content_kind != 6) {
        return 0;
    }
    edit_tnode_t *item = ctx->tree.last_node;
    bool before = list->list_selected == item->id;
    bool focused = edit_ctx_is_focused(ctx);
    bool now = before || (select && list->list_selected == 0) || focused;
    if (now) {
        list->list_selected_node = item;
        if (!before) {
            list->list_selected = item->id;
            ctx->needs_settling = true;
        }
    }

    bool clicked = !ctx->consumed && ctx->mouse_click == 2 && edit_ctx_was_mouse_down(ctx);
    bool entered =
        focused && before && !ctx->consumed && ctx->has_key && ctx->key == (uint32_t)EDIT_VK_RETURN;
    bool activated = clicked || entered;
    if (activated) {
        ctx->consumed = true;
    }
    if (before && activated) {
        return 2;
    }
    if (now && !before) {
        return 1;
    }
    return 0;
}

int edit_ctx_list_item(edit_ctx_t *ctx, bool select, const char *text) {
    if (ctx == NULL) {
        return 0;
    }
    edit_ctx_styled_list_item_begin(ctx);
    edit_ctx_styled_add(ctx, text == NULL ? "" : text, text == NULL ? 0 : strlen(text));
    return edit_ctx_styled_list_item_end(ctx, select);
}

void edit_ctx_list_end(edit_ctx_t *ctx) {
    if (ctx == NULL) {
        return;
    }
    edit_ctx_block_end(ctx);
    edit_tnode_t *list = ctx->tree.last_node;
    if (list == NULL || list->content_kind != 6) {
        return;
    }
    bool contains_focus = edit_ctx_contains_focus(ctx);
    edit_tnode_t *selected_now = list->list_selected_node;
    edit_tnode_t *selected_next = selected_now != NULL ? selected_now : list->child_first;
    if (selected_next == NULL) {
        return;
    }
    if (contains_focus && !ctx->consumed && ctx->has_key) {
        edit_tnode_t *prev = edit_nodemap_get(&ctx->tui->prev_map, list->id);
        if (prev != NULL && selected_now != NULL) {
            bool consumed = true;
            uint32_t key = ctx->key;
            if (key == (uint32_t)EDIT_VK_PRIOR) {
                selected_next = selected_now;
                int32_t steps = prev->inner_clipped.bottom - prev->inner_clipped.top - 1;
                for (int32_t i = 0; i < steps; ++i) {
                    if (selected_next->sib_prev == NULL) {
                        break;
                    }
                    selected_next = selected_next->sib_prev;
                }
            } else if (key == (uint32_t)EDIT_VK_NEXT) {
                selected_next = selected_now;
                int32_t steps = prev->inner_clipped.bottom - prev->inner_clipped.top - 1;
                for (int32_t i = 0; i < steps; ++i) {
                    if (selected_next->sib_next == NULL) {
                        break;
                    }
                    selected_next = selected_next->sib_next;
                }
            } else if (key == (uint32_t)EDIT_VK_END) {
                selected_next = list->child_last != NULL ? list->child_last : selected_next;
            } else if (key == (uint32_t)EDIT_VK_HOME) {
                selected_next = list->child_first != NULL ? list->child_first : selected_next;
            } else if (key == (uint32_t)EDIT_VK_UP) {
                edit_tnode_t *alt =
                    selected_now->sib_prev != NULL ? selected_now->sib_prev : list->child_last;
                selected_next = alt != NULL ? alt : selected_next;
            } else if (key == (uint32_t)EDIT_VK_DOWN) {
                edit_tnode_t *alt =
                    selected_now->sib_next != NULL ? selected_now->sib_next : list->child_first;
                selected_next = alt != NULL ? alt : selected_next;
            } else {
                consumed = false;
            }
            if (consumed) {
                ctx->consumed = true;
            }
        }
    }
    if (selected_next != selected_now) {
        list->list_selected_node = selected_next;
    }
    // Mark the selected item (replace leading spaces with '>').
    if (selected_next->content_kind == 3 && selected_next->text_len > 0) {
        // Arena text is writable within the frame (same arena, no realloc).
        ((char *)selected_next->text_ptr)[0] = '>';
    }
    if (contains_focus) {
        selected_next->attributes.bg = edit_tui_indexed(ctx->tui, EDIT_FB_GREEN);
        selected_next->attributes.fg =
            edit_tui_contrasted(ctx->tui, edit_tui_indexed(ctx->tui, EDIT_FB_GREEN));
        edit_tui_steal_focus(ctx->tui, selected_next);
    }
}

void edit_ctx_scrollarea_begin(edit_ctx_t *ctx, const char *classname, edit_size_t intrinsic) {
    if (ctx == NULL) {
        return;
    }
    edit_ctx_block_begin(ctx, classname);
    edit_tnode_t *container = ctx->tree.last_node;
    if (container == NULL) {
        return;
    }
    container->content_kind = 2;
    container->scroll_offset.x = INT32_MIN;
    container->scroll_offset.y = INT32_MIN;
    container->scroll_drag_start = INT32_MIN;
    container->scroll_thumb = 0;
    if (intrinsic.width > 0 || intrinsic.height > 0) {
        container->intrinsic_size.width = intrinsic.width > 0 ? intrinsic.width : 0;
        container->intrinsic_size.height = intrinsic.height > 0 ? intrinsic.height : 0;
        container->intrinsic_set = true;
    }
    edit_ctx_block_begin(ctx, "content");
    // Attribute tweaks apply to the outer container (restore current).
    if (ctx->tree.last_node != NULL) {
        ctx->tree.last_node = container;
    }
}

void edit_ctx_scrollarea_scroll_to(edit_ctx_t *ctx, edit_point_t pos) {
    if (ctx == NULL || ctx->tree.last_node == NULL) {
        return;
    }
    edit_tnode_t *node = ctx->tree.last_node;
    if (node->content_kind != 2) {
        assert(false && "not a scrollarea");
        return;
    }
    node->scroll_offset = pos;
}

void edit_ctx_scrollarea_end(edit_ctx_t *ctx) {
    if (ctx == NULL) {
        return;
    }
    edit_ctx_block_end(ctx); // content
    edit_ctx_block_end(ctx); // container
    edit_tnode_t *container = ctx->tree.last_node;
    if (container == NULL || container->content_kind != 2) {
        return;
    }
    uint64_t cid = container->id;
    int32_t cdepth = (int32_t)container->depth;
    edit_tnode_t *prev = edit_nodemap_get(&ctx->tui->prev_map, cid);
    if (prev == NULL) {
        return;
    }
    if (container->scroll_offset.x == INT32_MIN && container->scroll_offset.y == INT32_MIN &&
        prev->content_kind == 2) {
        container->scroll_offset = prev->scroll_offset;
        container->scroll_drag_start = prev->scroll_drag_start;
        container->scroll_thumb = prev->scroll_thumb;
    }
    if (ctx->consumed) {
        return;
    }
    if (ctx->tui->mouse_state != EDIT_MOUSE_NONE) {
        edit_rect_t crect = prev->inner;
        if (ctx->tui->mouse_state == EDIT_MOUSE_LEFT) {
            if (ctx->tui->mouse_is_drag) {
                edit_rect_t track = {crect.right, crect.top, crect.right + 1, crect.bottom};
                if (edit_rect_contains(track, ctx->tui->mouse_down_pos)) {
                    if (container->scroll_drag_start == INT32_MIN) {
                        container->scroll_drag_start = container->scroll_offset.y;
                    }
                    edit_tnode_t *content = prev->child_first;
                    if (content != NULL) {
                        int32_t content_h = content->inner.bottom - content->inner.top;
                        int32_t track_h = track.bottom - track.top;
                        int32_t scrollable = content_h - track_h;
                        if (scrollable > 0) {
                            int32_t trackable = track_h - container->scroll_thumb;
                            int32_t dy = ctx->tui->mouse_pos.y - ctx->tui->mouse_down_pos.y;
                            if (trackable > 0) {
                                container->scroll_offset.y =
                                    container->scroll_drag_start + (dy * scrollable) / trackable;
                            }
                        }
                    }
                    ctx->consumed = true;
                }
            }
        } else if (ctx->tui->mouse_state == EDIT_MOUSE_RELEASE) {
            container->scroll_drag_start = INT32_MIN;
        } else if (ctx->tui->mouse_state == EDIT_MOUSE_SCROLL) {
            if (edit_rect_contains(crect, ctx->tui->mouse_pos)) {
                container->scroll_offset.x += ctx->scroll_delta.x;
                container->scroll_offset.y += ctx->scroll_delta.y;
                ctx->consumed = true;
            }
        }
    } else {
        // Keyboard scrolling when the container subtree is focused.
        bool sub = false;
        if ((size_t)cdepth < ctx->tui->focus_len) {
            sub = ctx->tui->focus_path[cdepth] == cid;
        }
        if (sub && ctx->has_key) {
            bool consumed = true;
            int32_t h = prev->inner_clipped.bottom - prev->inner_clipped.top;
            if (ctx->key == (uint32_t)EDIT_VK_PRIOR) {
                container->scroll_offset.y -= h;
            } else if (ctx->key == (uint32_t)EDIT_VK_NEXT) {
                container->scroll_offset.y += h;
            } else if (ctx->key == (uint32_t)EDIT_VK_END) {
                container->scroll_offset.y = INT32_MAX;
            } else if (ctx->key == (uint32_t)EDIT_VK_HOME) {
                container->scroll_offset.y = 0;
            } else {
                consumed = false;
            }
            if (consumed) {
                ctx->consumed = true;
            }
        }
    }
}
