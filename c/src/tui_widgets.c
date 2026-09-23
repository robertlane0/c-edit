#include "edit/tui.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "edit/fb.h"
#include "edit/input.h"
#include "edit/measure.h"
#include "edit/uitext.h"

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
