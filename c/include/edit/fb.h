#ifndef EDIT_FB_H
#define EDIT_FB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "edit/helpers.h"

#ifdef __cplusplus
extern "C" {
#endif

// Terminal framebuffer with diff rendering (cf. Rust framebuffer.rs).
// Single-threaded; all drawing targets the back buffer after flip().
typedef enum {
    EDIT_FB_BLACK,
    EDIT_FB_RED,
    EDIT_FB_GREEN,
    EDIT_FB_YELLOW,
    EDIT_FB_BLUE,
    EDIT_FB_MAGENTA,
    EDIT_FB_CYAN,
    EDIT_FB_WHITE,
    EDIT_FB_BRIGHT_BLACK,
    EDIT_FB_BRIGHT_RED,
    EDIT_FB_BRIGHT_GREEN,
    EDIT_FB_BRIGHT_YELLOW,
    EDIT_FB_BRIGHT_BLUE,
    EDIT_FB_BRIGHT_MAGENTA,
    EDIT_FB_BRIGHT_CYAN,
    EDIT_FB_BRIGHT_WHITE,
    EDIT_FB_BACKGROUND,
    EDIT_FB_FOREGROUND,
} edit_fb_color_t;

#define EDIT_FB_COLORS 18

typedef uint8_t edit_fb_attr_t;
#define EDIT_FB_ATTR_NONE 0
#define EDIT_FB_ATTR_ITALIC 1
#define EDIT_FB_ATTR_UNDERLINED 2
#define EDIT_FB_ATTR_ALL 3

extern const uint32_t EDIT_FB_DEFAULT_THEME[EDIT_FB_COLORS];

typedef struct {
    uint32_t indexed[EDIT_FB_COLORS];
    uint32_t auto_colors[2];
    uint32_t contrast_cache[256][2]; // [slot] = {color, contrast}
    bool contrast_valid[256];
    uint32_t background_fill;
    uint32_t foreground_fill;
    void *impl; // owned backing store (buffers + render output)
    size_t frame;
} edit_fb_t;

int edit_fb_init(edit_fb_t *f);
void edit_fb_destroy(edit_fb_t *f);
void edit_fb_set_theme(edit_fb_t *f, const uint32_t colors[EDIT_FB_COLORS]);
int edit_fb_flip(edit_fb_t *f, edit_size_t size);
int edit_fb_replace_text(edit_fb_t *f, int32_t y, int32_t origin_x, int32_t clip_right,
                         const char *text, size_t len);
// Scrollbar thumb height in rows.
int32_t edit_fb_scrollbar(edit_fb_t *f, edit_rect_t clip, edit_rect_t track, int32_t content_offset,
                          int32_t content_height);
uint32_t edit_fb_indexed(const edit_fb_t *f, edit_fb_color_t index);
uint32_t edit_fb_indexed_alpha(const edit_fb_t *f, edit_fb_color_t index, uint32_t num,
                               uint32_t den);
uint32_t edit_fb_contrasted(edit_fb_t *f, uint32_t color);
void edit_fb_blend_bg(edit_fb_t *f, edit_rect_t target, uint32_t bg);
void edit_fb_blend_fg(edit_fb_t *f, edit_rect_t target, uint32_t fg);
void edit_fb_reverse(edit_fb_t *f, edit_rect_t target);
void edit_fb_replace_attr(edit_fb_t *f, edit_rect_t target, edit_fb_attr_t mask,
                          edit_fb_attr_t attr);
void edit_fb_set_cursor(edit_fb_t *f, edit_point_t pos, bool overtype);
// VT diff since last flip; output borrowed (valid until next render/flip).
size_t edit_fb_render(edit_fb_t *f, const char **out);

#ifdef __cplusplus
}
#endif

#endif
