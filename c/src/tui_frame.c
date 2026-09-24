#define _POSIX_C_SOURCE 200809L // clock_gettime; must precede headers

#include "edit/tui.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "edit/hash.h"

#define TUI_ARENA_BYTES ((size_t)128 * 1024 * 1024)
#define TUI_CLICK_MS 500

static int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static bool path_push(uint64_t **path, size_t *len, size_t *cap, uint64_t id) {
    if (*len == *cap) {
        size_t grown = *cap != 0 ? *cap * 2 : 16;
        uint64_t *nb = (uint64_t *)realloc(*path, grown * sizeof *nb);
        if (nb == NULL) {
            return false;
        }
        *path = nb;
        *cap = grown;
    }
    (*path)[(*len)++] = id;
    return true;
}

static void build_path(edit_tnode_t *node, uint64_t **path, size_t *len, size_t *cap) {
    *len = 0;
    if (node == NULL) {
        path_push(path, len, cap, EDIT_ROOT_ID);
        return;
    }
    // Leaf-first, then reverse to root-first.
    size_t start = *len;
    for (edit_tnode_t *n = node; n != NULL; n = n->parent) {
        if (!path_push(path, len, cap, n->id)) {
            break;
        }
    }
    for (size_t i = start, j = *len; i < --j; ++i) {
        uint64_t tmp = (*path)[i];
        (*path)[i] = (*path)[j];
        (*path)[j] = tmp;
    }
}

int edit_tui_init(edit_tui_t *t) {
    if (t == NULL) {
        return -1;
    }
    memset(t, 0, sizeof *t);
    for (size_t i = 0; i < 2; ++i) {
        if (edit_arena_init(&t->arenas[i], TUI_ARENA_BYTES) != 0) {
            for (size_t j = 0; j < i; ++j) {
                edit_arena_destroy(&t->arenas[j]);
            }
            return -1;
        }
    }
    t->prev_idx = 1;
    edit_tree_init(&t->prev_tree, &t->arenas[1]);
    if (edit_fb_init(&t->framebuffer) != 0) {
        edit_arena_destroy(&t->arenas[0]);
        edit_arena_destroy(&t->arenas[1]);
        return -1;
    }
    t->mod_ctrl = "Ctrl";
    t->mod_alt = "Alt";
    t->mod_shift = "Shift";
    t->mouse_pos.x = INT32_MIN;
    t->mouse_pos.y = INT32_MIN;
    t->mouse_down_pos.x = INT32_MIN;
    t->mouse_down_pos.y = INT32_MIN;
    t->mouse_up_ms = now_ms();
    t->first_click_pos.x = INT32_MIN;
    t->first_click_pos.y = INT32_MIN;
    build_path(NULL, &t->mouse_down_path, &t->mouse_down_len, &t->mouse_down_cap);
    build_path(NULL, &t->focus_path, &t->focus_len, &t->focus_cap);
    t->focus_scrolling = EDIT_ROOT_ID;
    t->read_timeout = EDIT_TUI_FOREVER;
    return 0;
}

void edit_tui_destroy(edit_tui_t *t) {
    if (t == NULL) {
        return;
    }
    edit_nodemap_destroy(&t->prev_map);
    edit_tree_destroy(&t->prev_tree);
    edit_fb_destroy(&t->framebuffer);
    for (size_t i = 0; i < t->tbuf_cache_len; ++i) {
        edit_shared_release(t->tbuf_cache[i].editor);
    }
    free(t->tbuf_cache);
    edit_arena_destroy(&t->arenas[0]);
    edit_arena_destroy(&t->arenas[1]);
    free(t->mouse_down_path);
    free(t->focus_path);
    free(t->clipboard);
    memset(t, 0, sizeof *t);
}

static bool needs_settling_now(const edit_tui_t *t) {
    return t->settling_have <= t->settling_want;
}

static void needs_more_settling(edit_tui_t *t) {
    if (t->settling_have == 15) {
        assert(t->settling_have != 15 && "settling deadlock?");
    }
    int32_t want = t->settling_have + 1;
    t->settling_want = want < 20 ? want : 20;
}

static bool scroll_to_focused(edit_tui_t *t) {
    uint64_t focused = t->focus_len > 0 ? t->focus_path[t->focus_len - 1] : EDIT_ROOT_ID;
    if (t->focus_scrolling == focused) {
        return false;
    }
    edit_tnode_t *node = edit_nodemap_get(&t->prev_map, focused);
    if (node == NULL) {
        return true;
    }
    edit_rect_t scroll_to = node->outer;
    while (node->parent != NULL && !node->attributes.float_attr.has_float) {
        if (node->content_kind == 2) {
            int32_t off = node->scroll_offset.y > 0 ? node->scroll_offset.y : 0;
            int32_t y = off;
            int32_t lo = scroll_to.top - node->inner.top + off;
            int32_t hi = scroll_to.bottom - node->inner.bottom + off;
            if (y > lo) {
                y = lo;
            }
            if (y < hi) {
                y = hi;
            }
            node->scroll_offset.y = y;
            scroll_to = node->outer;
        }
        node = node->parent;
    }
    t->focus_scrolling = focused;
    return true;
}

static bool pop_focusable(edit_tui_t *t, size_t pop_min) {
    uint64_t before = t->focus_len > 0 ? t->focus_path[t->focus_len - 1] : 0;
    size_t keep = t->focus_len > pop_min ? t->focus_len - pop_min : 0;
    size_t len = 0;
    for (size_t i = 0; i < keep; ++i) {
        edit_tnode_t *node = edit_nodemap_get(&t->prev_map, t->focus_path[i]);
        if (node == NULL) {
            break;
        }
        if (pop_min != 0 && node->attributes.focus_void) {
            break;
        }
        if (node->attributes.focusable) {
            len = i + 1;
        }
    }
    t->focus_len = len;
    if (t->focus_len == 0) {
        path_push(&t->focus_path, &t->focus_len, &t->focus_cap, EDIT_ROOT_ID);
    }
    uint64_t after = t->focus_len > 0 ? t->focus_path[t->focus_len - 1] : 0;
    return before != after;
}

bool edit_tui_pop_focusable(edit_tui_t *t, size_t pop_min) {
    if (t == NULL) {
        return false;
    }
    return pop_focusable(t, pop_min);
}

typedef struct {
    edit_tnode_t *next;
    edit_tnode_t *root;
    edit_tnode_t *start;
    bool forward;
    size_t min_depth;
} focus_visit_t;

static edit_visit_t focus_visitor(edit_tnode_t *node, void *ctx) {
    focus_visit_t *v = (focus_visit_t *)ctx;
    if (node == v->root) {
        return EDIT_VISIT_CONTINUE;
    }
    if (node->attributes.focusable && node != v->start) {
        v->next = node;
        return EDIT_VISIT_STOP;
    }
    if (node->attributes.focus_void || node->depth >= v->min_depth) {
        return EDIT_VISIT_SKIP;
    }
    return EDIT_VISIT_CONTINUE;
}

// Tab/arrows focus movement; true if focus changed.
static bool move_focus(edit_tui_t *t, uint32_t key) {
    bool is_tab = key == (uint32_t)EDIT_VK_TAB || key == ((uint32_t)EDIT_KBMOD_SHIFT | EDIT_VK_TAB);
    bool is_updown = key == (uint32_t)EDIT_VK_UP || key == (uint32_t)EDIT_VK_DOWN;
    bool is_leftright = key == (uint32_t)EDIT_VK_LEFT || key == (uint32_t)EDIT_VK_RIGHT;
    if (!is_tab && !is_updown && !is_leftright) {
        return false;
    }
    uint64_t focused_id = t->focus_len > 0 ? t->focus_path[t->focus_len - 1] : 0;
    edit_tnode_t *focused = edit_nodemap_get(&t->prev_map, focused_id);
    if (focused == NULL) {
        assert(false && "focus path not cleaned");
        return false;
    }
    edit_tnode_t *start = focused;
    edit_tnode_t *root = focused;
    for (;;) {
        if (root->attributes.focus_well) {
            break;
        }
        if (root->attributes.focus_void) {
            start = root;
        }
        if (root->parent == NULL) {
            break;
        }
        root = root->parent;
    }
    bool forward = true;
    size_t min_depth = (size_t)(-1);
    if (is_tab) {
        forward = key == (uint32_t)EDIT_VK_TAB;
    } else if (is_updown) {
        forward = key == (uint32_t)EDIT_VK_DOWN;
    } else {
        // LEFT/RIGHT move within the enclosing table row.
        edit_tnode_t *buf[3] = {NULL, NULL, NULL};
        size_t idx = 2;
        edit_tnode_t *node = start;
        for (;;) {
            idx = (idx + 1) % 3;
            buf[idx] = node;
            if (node->content_kind == 1) {
                break;
            }
            if (node == root || node->parent == NULL) {
                return false;
            }
            node = node->parent;
        }
        edit_tnode_t *row = buf[(idx + 3 - 1) % 3];
        edit_tnode_t *cell = buf[(idx + 3 - 2) % 3];
        if (row == NULL || cell == NULL) {
            return false;
        }
        root = row;
        start = cell;
        forward = key == (uint32_t)EDIT_VK_RIGHT;
        min_depth = row->depth;
    }
    focus_visit_t v = {focused, root, start, forward, min_depth};
    // Note: visit needs start; reuse fields (next doubles as result init).
    v.next = start;
    edit_tree_visit(root, start, forward, focus_visitor, &v);
    if (v.next == start) {
        return false;
    }
    build_path(v.next, &t->focus_path, &t->focus_len, &t->focus_cap);
    return true;
}

typedef struct {
    edit_point_t pos;
    edit_tnode_t *hovered;
    edit_tnode_t *focused;
} hover_ctx_t;

static edit_visit_t hover_visitor(edit_tnode_t *node, void *ctx) {
    hover_ctx_t *h = (hover_ctx_t *)ctx;
    if (!edit_rect_contains(node->outer_clipped, h->pos)) {
        return EDIT_VISIT_SKIP;
    }
    h->hovered = node;
    if (node->attributes.focusable) {
        h->focused = node;
    }
    return EDIT_VISIT_CONTINUE;
}

int edit_tui_begin(edit_tui_t *t, const edit_input_t *input, edit_ctx_t *ctx) {
    if (t == NULL || ctx == NULL) {
        return -1;
    }
    memset(ctx, 0, sizeof *ctx);
    // Swap arenas: the new frame builds in the other region.
    size_t next_idx = t->prev_idx ^ 1;
    edit_arena_reset(&t->arenas[next_idx], 0);
    t->prev_tree.arena = &t->arenas[t->prev_idx];

    // Release/scroll mouse states age out one frame later.
    if (t->mouse_state > EDIT_MOUSE_RIGHT) {
        t->mouse_down_pos.x = INT32_MIN;
        t->mouse_down_pos.y = INT32_MIN;
        t->mouse_down_len = 0;
        t->mouse_down_target = 0;
        t->mouse_state = EDIT_MOUSE_NONE;
        t->mouse_is_drag = false;
    }

    if (scroll_to_focused(t)) {
        needs_more_settling(t);
    }

    int64_t now = now_ms();
    const uint8_t *text_ptr = NULL;
    size_t text_len = 0;
    bool text_bracketed = false;
    bool has_text = false;
    uint32_t key = 0;
    bool has_key = false;
    uint32_t mouse_mods = EDIT_KBMOD_NONE;
    int32_t mouse_click = 0;
    edit_point_t scroll_delta = {0, 0};
    bool consumed = needs_settling_now(t);

    if (input != NULL) {
        if (input->kind == EDIT_IN_RESIZE) {
            int32_t w = input->size.width;
            int32_t h = input->size.height;
            assert(w > 0 && h > 0 && w < 32767 && h < 32767);
            if (w > 0 && h > 0 && w < 32767 && h < 32767) {
                t->size.width = w;
                t->size.height = h;
            }
        } else if (input->kind == EDIT_IN_TEXT) {
            has_text = true;
            text_ptr = input->text.ptr;
            text_len = input->text.len;
            text_bracketed = input->text.bracketed;
            if (!text_bracketed && text_len == 1) {
                uint32_t ch = text_ptr[0];
                edit_key_t k = 0;
                if (edit_key_from_ascii(ch, &k)) {
                    has_key = true;
                    key = k;
                }
            }
        } else if (input->kind == EDIT_IN_KEYBOARD) {
            has_key = true;
            key = input->key;
        } else if (input->kind == EDIT_IN_MOUSE) {
            int next_state = (int)input->mouse.state;
            edit_point_t next_pos = input->mouse.position;
            edit_point_t next_scroll = input->mouse.scroll;
            bool mouse_down = t->mouse_state == EDIT_MOUSE_NONE && next_state != EDIT_MOUSE_NONE;
            bool mouse_up = t->mouse_state != EDIT_MOUSE_NONE && next_state == EDIT_MOUSE_NONE;
            bool is_drag = t->mouse_state == EDIT_MOUSE_LEFT && next_state == EDIT_MOUSE_LEFT &&
                           (next_pos.x != t->mouse_pos.x || next_pos.y != t->mouse_pos.y);

            edit_tnode_t *hovered = NULL;
            edit_tnode_t *focused_node = NULL;
            if (mouse_down || mouse_up) {
                hover_ctx_t h = {next_pos, NULL, NULL};
                for (edit_tnode_t *root = t->prev_tree.root_first; root != NULL;
                     root = root->sib_next) {
                    edit_tree_visit(root, root, true, hover_visitor, &h);
                }
                hovered = h.hovered;
                focused_node = h.focused;
            }
            if (mouse_down) {
                build_path(hovered, &t->mouse_down_path, &t->mouse_down_len, &t->mouse_down_cap);
                uint64_t target = 0;
                if (next_state == EDIT_MOUSE_LEFT) {
                    target = focused_node != NULL ? focused_node->id : 0;
                    build_path(focused_node, &t->focus_path, &t->focus_len, &t->focus_cap);
                    needs_more_settling(t);
                }
                if (t->mouse_clicks != 0) {
                    if (t->first_click_target != target || t->first_click_pos.x != next_pos.x ||
                        t->first_click_pos.y != next_pos.y || now - t->mouse_up_ms > TUI_CLICK_MS) {
                        t->mouse_clicks = 0;
                        t->first_click_pos.x = INT32_MIN;
                        t->first_click_pos.y = INT32_MIN;
                        t->first_click_target = 0;
                    } else {
                        t->mouse_clicks += 1;
                        mouse_click = t->mouse_clicks;
                    }
                }
                t->mouse_down_target = target;
                t->mouse_down_pos = next_pos;
            } else if (mouse_up) {
                next_state = EDIT_MOUSE_RELEASE;
                uint64_t target = focused_node != NULL ? focused_node->id : 0;
                if (t->mouse_down_target == 0 || t->mouse_down_target != target) {
                    t->mouse_clicks = 0;
                    t->first_click_pos.x = INT32_MIN;
                    t->first_click_pos.y = INT32_MIN;
                    t->first_click_target = 0;
                } else if (t->mouse_clicks == 0) {
                    t->mouse_clicks = 1;
                    t->first_click_pos = t->mouse_down_pos;
                    t->first_click_target = target;
                    mouse_click = 1;
                }
                t->mouse_up_ms = now;
            } else if (is_drag) {
                t->mouse_is_drag = true;
            }
            mouse_mods = input->mouse.modifiers;
            scroll_delta = next_scroll;
            t->mouse_pos = next_pos;
            t->mouse_state = (edit_mouse_state_t)next_state;
        }
    }
    if (!consumed) {
        t->settling_have = 0;
        t->settling_want = 1;
    }

    ctx->tui = t;
    ctx->text_ptr = text_ptr;
    ctx->text_len = text_len;
    ctx->text_bracketed = text_bracketed;
    ctx->has_text = has_text;
    ctx->key = key;
    ctx->has_key = has_key;
    ctx->mouse_mods = mouse_mods;
    ctx->mouse_click = mouse_click;
    ctx->scroll_delta = scroll_delta;
    ctx->consumed = consumed;
    edit_tree_init(&ctx->tree, &t->arenas[next_idx]);
    ctx->last_modal = NULL;
    ctx->id_mixin = 0;
    ctx->has_mixin = false;
    ctx->needs_settling = false;
    return 0;
}

static bool is_subtree_focused(const edit_tui_t *t, const edit_tnode_t *node) {
    if (node == NULL || node->depth >= t->focus_len) {
        return false;
    }
    return t->focus_path[node->depth] == node->id;
}

bool edit_tui_is_focused(const edit_tui_t *t, uint64_t id) {
    if (t == NULL || t->focus_len == 0) {
        return false;
    }
    return t->focus_path[t->focus_len - 1] == id;
}

void edit_tui_steal_focus(edit_tui_t *t, edit_tnode_t *node) {
    if (t == NULL || node == NULL) {
        return;
    }
    if (!edit_tui_is_focused(t, node->id)) {
        build_path(node, &t->focus_path, &t->focus_len, &t->focus_cap);
    }
}

void edit_tui_set_modifiers(edit_tui_t *t, const char *ctrl, const char *alt, const char *shift) {
    if (t == NULL) {
        return;
    }
    // Borrowed pointers; defaults are static strings.
    if (ctrl != NULL) {
        t->mod_ctrl = ctrl;
    }
    if (alt != NULL) {
        t->mod_alt = alt;
    }
    if (shift != NULL) {
        t->mod_shift = shift;
    }
}

void edit_ctx_toss_focus_up(edit_ctx_t *ctx) {
    if (ctx == NULL || ctx->tui == NULL) {
        return;
    }
    if (edit_tui_pop_focusable(ctx->tui, 1)) {
        ctx->needs_settling = true;
    }
}

void edit_ctx_needs_rerender(edit_ctx_t *ctx) {
    if (ctx == NULL || ctx->tui == NULL) {
        return;
    }
    assert(ctx->tui->settling_have < 15 && "rerender deadlock?");
    ctx->needs_settling = true;
}

void edit_tui_end(edit_tui_t *t, edit_ctx_t *ctx) {
    if (t == NULL || ctx == NULL) {
        return;
    }
    assert(ctx->tree.current_node == NULL || ctx->tree.current_node->stack_parent == NULL);
    // Keep modal focus inside the modal subtree.
    if (ctx->last_modal != NULL && !is_subtree_focused(t, ctx->last_modal)) {
        edit_tui_steal_focus(t, ctx->last_modal);
    }
    bool needs_settling = ctx->needs_settling;
    if (t->prev_tree.checksum != ctx->tree.checksum) {
        needs_settling = true;
    }
    // Adopt the new tree and rebuild the node map.
    t->prev_tree = ctx->tree;
    // The duplicate-ID set belongs to frame building only.
    free(ctx->tree.seen_ids);
    t->prev_tree.seen_ids = NULL;
    t->prev_tree.seen_len = 0;
    t->prev_tree.seen_cap = 0;
    edit_nodemap_destroy(&t->prev_map);
    edit_nodemap_build(&t->prev_map, &t->prev_tree);

    size_t pop_min = 0;
    // Escape pops focus to the parent node.
    if (!ctx->consumed && ctx->has_key && ctx->key == (uint32_t)EDIT_VK_ESCAPE) {
        ctx->consumed = true;
        pop_min = 1;
    }
    bool focus_changed = pop_focusable(t, pop_min);
    if (focus_changed) {
        needs_settling = true;
    }
    // Tab moves focus unless nodes vanished above.
    if (!focus_changed && !ctx->consumed && ctx->has_key) {
        if (move_focus(t, ctx->key)) {
            needs_settling = true;
        }
    }
    t->settling_have += 1;
    if (needs_settling) {
        needs_more_settling(t);
    }
    // Layout the adopted tree in the viewport.
    edit_rect_t viewport = {0, 0, t->size.width, t->size.height};
    edit_tree_layout(&t->prev_tree, viewport);
    t->prev_idx ^= 1;
    memset(ctx, 0, sizeof *ctx);
}

size_t edit_tui_render(edit_tui_t *t, const char **out) {
    if (out != NULL) {
        *out = NULL;
    }
    if (t == NULL) {
        return 0;
    }
    // Flip first so back/front roles match the Rust frame flow.
    if (edit_fb_flip(&t->framebuffer, t->size) != 0) {
        return 0;
    }
    edit_tui_draw(t);
    return edit_fb_render(&t->framebuffer, out);
}

int64_t edit_tui_read_timeout(edit_tui_t *t) {
    if (t == NULL) {
        return EDIT_TUI_FOREVER;
    }
    int64_t timeout = t->read_timeout;
    t->read_timeout = EDIT_TUI_FOREVER;
    return timeout;
}

edit_size_t edit_tui_size(const edit_tui_t *t) {
    edit_size_t z = {0, 0};
    return t == NULL ? z : t->size;
}

uint32_t edit_tui_indexed(edit_tui_t *t, edit_fb_color_t index) {
    return edit_fb_indexed(t == NULL ? NULL : &t->framebuffer, index);
}

uint32_t edit_tui_indexed_alpha(edit_tui_t *t, edit_fb_color_t index, uint32_t num, uint32_t den) {
    return edit_fb_indexed_alpha(t == NULL ? NULL : &t->framebuffer, index, num, den);
}

uint32_t edit_tui_contrasted(edit_tui_t *t, uint32_t color) {
    return edit_fb_contrasted(t == NULL ? NULL : &t->framebuffer, color);
}

const uint8_t *edit_tui_clipboard(const edit_tui_t *t, size_t *out_len) {
    if (out_len != NULL) {
        *out_len = t == NULL ? 0 : t->clipboard_len;
    }
    return t == NULL ? NULL : t->clipboard;
}

uint32_t edit_tui_clipboard_gen(const edit_tui_t *t) {
    return t == NULL ? 0 : t->clipboard_gen;
}

void edit_tui_set_clipboard(edit_tui_t *t, const uint8_t *data, size_t len) {
    if (t == NULL || data == NULL || len == 0) {
        return;
    }
    if (len > t->clipboard_cap) {
        size_t grown = len;
        uint8_t *nb = (uint8_t *)realloc(t->clipboard, grown);
        if (nb == NULL) {
            return;
        }
        t->clipboard = nb;
        t->clipboard_cap = grown;
    }
    memcpy(t->clipboard, data, len);
    t->clipboard_len = len;
    t->clipboard_gen += 1;
}

bool edit_tui_needs_settling(edit_tui_t *t) {
    if (t == NULL) {
        return false;
    }
    return needs_settling_now(t);
}
