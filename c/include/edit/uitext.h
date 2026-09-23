#ifndef EDIT_UITEXT_H
#define EDIT_UITEXT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "edit/fb.h"
#include "edit/helpers.h"

#ifdef __cplusplus
extern "C" {
#endif

// Styled label text (cf. Rust TextContent + render_styled_text).
// Heap-owned; empty-fg/attr sentinel matches INVALID_STYLED_TEXT_CHUNK.
typedef enum {
    EDIT_OVF_CLIP,
    EDIT_OVF_HEAD,   // ellipsis first ("…tail")
    EDIT_OVF_MIDDLE, // ellipsis inside ("he…lo")
    EDIT_OVF_TAIL,   // ellipsis last ("hea…")
} edit_overflow_t;

typedef struct {
    size_t offset;
    uint32_t fg;
    uint8_t attr;
} edit_text_chunk_t;

typedef struct {
    char *text;
    size_t len;
    size_t cap;
    edit_text_chunk_t *chunks;
    size_t nchunks;
    size_t chunkcap;
    edit_overflow_t overflow;
} edit_text_content_t;

void edit_text_init(edit_text_content_t *c);
void edit_text_destroy(edit_text_content_t *c);
bool edit_text_add(edit_text_content_t *c, const char *text, size_t len);
// Pencil color/attributes with Rust's dedup rules.
bool edit_text_set_fg(edit_text_content_t *c, uint32_t fg);
bool edit_text_set_attr(edit_text_content_t *c, uint8_t attr);
void edit_text_set_overflow(edit_text_content_t *c, edit_overflow_t overflow);
// Visual width of the full text (single line).
size_t edit_text_measure(const char *text, size_t len);
// Draws into fb (actual_width = full visual width, usually measured).
bool edit_text_render(const edit_text_content_t *c, edit_rect_t target, size_t actual_width,
                      edit_fb_t *fb);

#ifdef __cplusplus
}
#endif

#endif
