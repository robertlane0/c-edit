#include "edit/uitext.h"

#include <stdlib.h>
#include <string.h>

#include "edit/doc.h"
#include "edit/measure.h"

void edit_text_init(edit_text_content_t *c) {
    if (c != NULL) {
        memset(c, 0, sizeof *c);
    }
}

void edit_text_destroy(edit_text_content_t *c) {
    if (c == NULL) {
        return;
    }
    free(c->text);
    free(c->chunks);
    memset(c, 0, sizeof *c);
}

static bool text_reserve(edit_text_content_t *c, size_t additional) {
    if (additional > (size_t)(-1) - c->len) {
        return false;
    }
    size_t need = c->len + additional;
    if (need > c->cap) {
        size_t grown = c->cap != 0 ? c->cap : 32;
        while (grown < need) {
            if (grown > (size_t)(-1) / 2) {
                grown = need;
                break;
            }
            grown *= 2;
        }
        char *nb = (char *)realloc(c->text, grown);
        if (nb == NULL) {
            return false;
        }
        c->text = nb;
        c->cap = grown;
    }
    return true;
}

bool edit_text_add(edit_text_content_t *c, const char *text, size_t len) {
    if (c == NULL) {
        return false;
    }
    if (len == 0) {
        return true;
    }
    if (text == NULL || !text_reserve(c, len)) {
        return false;
    }
    memcpy(c->text + c->len, text, len);
    c->len += len;
    return true;
}

static bool chunk_push(edit_text_content_t *c, size_t offset, uint32_t fg, uint8_t attr) {
    if (c->nchunks == c->chunkcap) {
        size_t grown = c->chunkcap != 0 ? c->chunkcap * 2 : 4;
        edit_text_chunk_t *nb = (edit_text_chunk_t *)realloc(c->chunks, grown * sizeof *nb);
        if (nb == NULL) {
            return false;
        }
        c->chunks = nb;
        c->chunkcap = grown;
    }
    c->chunks[c->nchunks].offset = offset;
    c->chunks[c->nchunks].fg = fg;
    c->chunks[c->nchunks].attr = attr;
    c->nchunks += 1;
    return true;
}

bool edit_text_set_fg(edit_text_content_t *c, uint32_t fg) {
    if (c == NULL) {
        return false;
    }
    size_t last_off = (size_t)(-1);
    uint32_t last_fg = 0;
    uint8_t last_attr = 0;
    if (c->nchunks > 0) {
        last_off = c->chunks[c->nchunks - 1].offset;
        last_fg = c->chunks[c->nchunks - 1].fg;
        last_attr = c->chunks[c->nchunks - 1].attr;
    }
    if (last_off != c->len && last_fg != fg) {
        return chunk_push(c, c->len, fg, last_attr);
    }
    return true;
}

bool edit_text_set_attr(edit_text_content_t *c, uint8_t attr) {
    if (c == NULL) {
        return false;
    }
    size_t last_off = (size_t)(-1);
    uint32_t last_fg = 0;
    uint8_t last_attr = 0;
    if (c->nchunks > 0) {
        last_off = c->chunks[c->nchunks - 1].offset;
        last_fg = c->chunks[c->nchunks - 1].fg;
        last_attr = c->chunks[c->nchunks - 1].attr;
    }
    if (last_off != c->len && last_attr != attr) {
        return chunk_push(c, c->len, last_fg, attr);
    }
    return true;
}

void edit_text_set_overflow(edit_text_content_t *c, edit_overflow_t overflow) {
    if (c != NULL) {
        c->overflow = overflow;
    }
}

// Slice doc over borrowed bytes for measurement.
typedef struct {
    edit_doc_t doc;
    const uint8_t *bytes;
    size_t len;
} text_slice_t;

static size_t ts_len(const void *ctx) {
    return ((const text_slice_t *)ctx)->len;
}

static void ts_fwd(const void *ctx, size_t off, const uint8_t **p, size_t *n) {
    const text_slice_t *s = (const text_slice_t *)ctx;
    size_t o = off < s->len ? off : s->len;
    *p = s->bytes + o;
    *n = s->len - o;
}

static void text_measure_init(const uint8_t *bytes, size_t len, edit_measure_t *m, edit_doc_t *doc,
                              text_slice_t *slice) {
    slice->bytes = bytes;
    slice->len = len;
    slice->doc.ctx = slice;
    slice->doc.len = ts_len;
    slice->doc.read_fwd = ts_fwd;
    slice->doc.read_bwd = NULL;
    slice->doc.replace = NULL;
    edit_measure_init(m, &slice->doc);
    *doc = slice->doc;
}

size_t edit_text_measure(const char *text, size_t len) {
    if (text == NULL || len == 0) {
        return 0;
    }
    text_slice_t slice;
    edit_measure_t m;
    edit_doc_t doc;
    text_measure_init((const uint8_t *)text, len, &m, &doc, &slice);
    edit_point_t max = {INT32_MAX, INT32_MAX};
    edit_cursor_t cur = edit_measure_goto_visual(&m, max);
    return cur.visual.x < 0 ? 0 : (size_t)cur.visual.x;
}

bool edit_text_render(const edit_text_content_t *c, edit_rect_t target, size_t actual_width,
                      edit_fb_t *fb) {
    if (c == NULL || fb == NULL) {
        return false;
    }
    const uint8_t *bytes = c->text != NULL ? (const uint8_t *)c->text : (const uint8_t *)"";
    size_t len = c->text != NULL ? c->len : 0;
    int64_t target_width = (int64_t)target.right - (int64_t)target.left;
    if (target_width < 0) {
        target_width = 0;
    }

    size_t skip_beg = 0;
    size_t skip_end = 0;
    int64_t skip_cols = 0;

    // Heap scratch for the ellipsis-modified line.
    uint8_t *modified = NULL;
    const uint8_t *draw = bytes;
    size_t draw_len = len;

    if (!(c->overflow == EDIT_OVF_CLIP || target_width >= (int64_t)actual_width)) {
        text_slice_t slice;
        edit_measure_t m;
        edit_doc_t doc;
        text_measure_init(bytes, len, &m, &doc, &slice);
        if (c->overflow == EDIT_OVF_HEAD) {
            int64_t x = (int64_t)actual_width - target_width + 1;
            edit_point_t goal = {x > INT32_MAX ? INT32_MAX : (int32_t)(x < 0 ? 0 : x), 0};
            edit_cursor_t beg = edit_measure_goto_visual(&m, goal);
            skip_beg = 0;
            skip_end = beg.offset;
            skip_cols = (int64_t)beg.visual.x - 1;
        } else if (c->overflow == EDIT_OVF_MIDDLE) {
            int64_t mb = (target_width - 1) / 2;
            int64_t me = (int64_t)actual_width - target_width / 2;
            edit_point_t gb = {mb > INT32_MAX ? INT32_MAX : (int32_t)(mb < 0 ? 0 : mb), 0};
            edit_point_t ge = {me > INT32_MAX ? INT32_MAX : (int32_t)(me < 0 ? 0 : me), 0};
            edit_cursor_t beg = edit_measure_goto_visual(&m, gb);
            edit_cursor_t end = edit_measure_goto_visual(&m, ge);
            skip_beg = beg.offset;
            skip_end = end.offset;
            skip_cols = (int64_t)end.visual.x - (int64_t)beg.visual.x - 1;
        } else {
            int64_t x = target_width - 1;
            edit_point_t goal = {x > INT32_MAX ? INT32_MAX : (int32_t)(x < 0 ? 0 : x), 0};
            edit_cursor_t end = edit_measure_goto_visual(&m, goal);
            skip_cols = (int64_t)actual_width - (int64_t)end.visual.x - 1;
            skip_beg = end.offset;
            skip_end = len;
        }
        if (skip_end < skip_beg) {
            skip_end = skip_beg;
        }
        size_t need = skip_beg + 3 + (len - skip_end);
        modified = (uint8_t *)malloc(need > 0 ? need : 1);
        if (modified == NULL) {
            return false;
        }
        memcpy(modified, bytes, skip_beg);
        memcpy(modified + skip_beg, "\xE2\x80\xA6", 3);
        memcpy(modified + skip_beg + 3, bytes + skip_end, len - skip_end);
        draw = modified;
        draw_len = need;
    }

    bool ok = edit_fb_replace_text(fb, target.top, target.left, target.right, (const char *)draw,
                                   draw_len) == 0;

    if (ok && c->nchunks > 0) {
        text_slice_t slice;
        edit_measure_t m;
        edit_doc_t doc;
        text_measure_init(bytes, len, &m, &doc, &slice);
        // Cursor starts at the target's left edge for absolute rects.
        edit_measure_set_cursor(&m, (edit_cursor_t){0, {0, 0}, {target.left, 0}, 0, false});
        for (size_t i = 0; ok && i < c->nchunks; ++i) {
            size_t beg = c->chunks[i].offset;
            size_t end = (i + 1 < c->nchunks) ? c->chunks[i + 1].offset : len;
            if (beg >= skip_beg && end <= skip_end && skip_beg < skip_end) {
                continue;
            }
            if (beg < skip_beg) {
                size_t e2 = end < skip_beg ? end : skip_beg;
                edit_cursor_t b = edit_measure_goto_offset(&m, beg);
                edit_cursor_t e = edit_measure_goto_offset(&m, e2);
                edit_rect_t r = {b.visual.x, target.top, e.visual.x, target.bottom};
                edit_fb_blend_fg(fb, r, c->chunks[i].fg);
                edit_fb_replace_attr(fb, r, c->chunks[i].attr, c->chunks[i].attr);
            }
            if (end > skip_end) {
                size_t b2 = beg > skip_end ? beg : skip_end;
                edit_cursor_t b = edit_measure_goto_offset(&m, b2);
                edit_cursor_t e = edit_measure_goto_offset(&m, end);
                int64_t bx = (int64_t)b.visual.x - skip_cols;
                int64_t ex = (int64_t)e.visual.x - skip_cols;
                edit_rect_t r = {(int32_t)(bx < INT32_MIN ? INT32_MIN : bx), target.top,
                                 (int32_t)(ex < INT32_MIN ? INT32_MIN : ex), target.bottom};
                edit_fb_blend_fg(fb, r, c->chunks[i].fg);
                edit_fb_replace_attr(fb, r, c->chunks[i].attr, c->chunks[i].attr);
            }
        }
    }
    free(modified);
    return ok;
}
