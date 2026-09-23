#ifndef EDIT_TUI_H
#define EDIT_TUI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "edit/arena.h"
#include "edit/fb.h"
#include "edit/helpers.h"
#include "edit/input.h"
#include "edit/tree.h"

#ifdef __cplusplus
extern "C" {
#endif

// Retained-mode TUI frame state (cf. Rust tui::Tui/Context frame core).
// Times in ms (monotonic); EDIT_TUI_FOREVER = no timeout.
#define EDIT_TUI_FOREVER ((int64_t)INT64_MAX)

typedef struct {
    edit_arena_t arenas[2];
    size_t prev_idx;
    edit_tree_t prev_tree;
    edit_nodemap_t prev_map;
    edit_fb_t framebuffer;
    const char *mod_ctrl;
    const char *mod_alt;
    const char *mod_shift;
    uint32_t floater_bg;
    uint32_t floater_fg;
    uint32_t modal_bg;
    uint32_t modal_fg;
    edit_size_t size;
    edit_point_t mouse_pos;
    edit_point_t mouse_down_pos;
    uint64_t mouse_down_target;
    int64_t mouse_up_ms;
    edit_mouse_state_t mouse_state;
    bool mouse_is_drag;
    int32_t mouse_clicks;
    uint64_t *mouse_down_path;
    size_t mouse_down_len;
    size_t mouse_down_cap;
    edit_point_t first_click_pos;
    uint64_t first_click_target;
    uint64_t *focus_path;
    size_t focus_len;
    size_t focus_cap;
    uint64_t focus_scrolling;
    uint8_t *clipboard;
    size_t clipboard_len;
    size_t clipboard_cap;
    uint32_t clipboard_gen;
    int32_t settling_have;
    int32_t settling_want;
    int64_t read_timeout;
} edit_tui_t;

typedef struct {
    edit_tui_t *tui;
    const uint8_t *text_ptr;
    size_t text_len;
    bool text_bracketed;
    bool has_text;
    edit_key_t key;
    bool has_key;
    edit_keymod_t mouse_mods;
    int32_t mouse_click;
    edit_point_t scroll_delta;
    bool consumed;
    edit_tree_t tree;
    edit_tnode_t *last_modal;
    uint64_t id_mixin;
    bool has_mixin;
    bool needs_settling;
} edit_ctx_t;

int edit_tui_init(edit_tui_t *t);
void edit_tui_destroy(edit_tui_t *t);
// Frame begin/end (end runs tree swap, focus, layout driver prep).
// Input bytes are borrowed for the frame only.
int edit_tui_begin(edit_tui_t *t, const edit_input_t *input, edit_ctx_t *ctx);
void edit_tui_end(edit_tui_t *t, edit_ctx_t *ctx);
// VT output of the last frame; borrowed until next render/flip.
size_t edit_tui_render(edit_tui_t *t, const char **out);
int64_t edit_tui_read_timeout(edit_tui_t *t);
edit_size_t edit_tui_size(const edit_tui_t *t);
// Palette passthroughs.
uint32_t edit_tui_indexed(edit_tui_t *t, edit_fb_color_t index);
uint32_t edit_tui_indexed_alpha(edit_tui_t *t, edit_fb_color_t index, uint32_t num, uint32_t den);
uint32_t edit_tui_contrasted(edit_tui_t *t, uint32_t color);
// Clipboard (borrowed contents; generation bump on set).
const uint8_t *edit_tui_clipboard(const edit_tui_t *t, size_t *out_len);
uint32_t edit_tui_clipboard_gen(const edit_tui_t *t);
void edit_tui_set_clipboard(edit_tui_t *t, const uint8_t *data, size_t len);
// Redraw until false.
bool edit_tui_needs_settling(edit_tui_t *t);
// Focus helpers for widgets.
bool edit_tui_is_focused(const edit_tui_t *t, uint64_t id);
void edit_tui_steal_focus(edit_tui_t *t, edit_tnode_t *node);

#ifdef __cplusplus
}
#endif

#endif
