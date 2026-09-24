#include "edit/fb.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "edit/doc.h"
#include "edit/measure.h"
#include "edit/oklab.h"
#include "edit/simd.h"

#define FB_CACHE_SHIFT 56
#define FB_HASH_MULT UINT64_C(6364136223846793005)

const uint32_t EDIT_FB_DEFAULT_THEME[EDIT_FB_COLORS] = {
    0xFF000000, 0xFF212CBE, 0xFF3AAE3F, 0xFF4A9ABE, 0xFFBE4D20, 0xFFBE54BB,
    0xFFB2A700, 0xFFBEBEBE, 0xFF808080, 0xFF303EFF, 0xFF51EA58, 0xFF44C9FF,
    0xFFFF6A2F, 0xFFFF74FC, 0xFFF0E100, 0xFFFFFFFF, 0xFF000000, 0xFFFFFFFF,
};

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} fb_line_t;

typedef struct {
    uint32_t *data;
    size_t count;
    edit_size_t size;
} fb_bitmap_t;

typedef struct {
    uint8_t *data;
    size_t count;
    edit_size_t size;
} fb_attrs_t;

typedef struct {
    int32_t x;
    int32_t y;
    bool overtype;
    bool disabled;
} fb_cursor_t;

struct edit_fb_buffer {
    fb_line_t *lines;
    size_t nlines;
    edit_size_t size;
    fb_bitmap_t bg;
    fb_bitmap_t fg;
    fb_attrs_t attrs;
    fb_cursor_t cursor;
};

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} fb_out_t;

struct fb_impl {
    struct edit_fb_buffer buffers[2];
    fb_out_t out;
};

static struct fb_impl *impl_of(edit_fb_t *f) {
    return (struct fb_impl *)f->impl;
}

// Heap byte-slice splice with space padding (mirrors LineBuffer splice).
static bool line_splice(fb_line_t *line, size_t del_beg, size_t del_end, const uint8_t *src,
                        size_t src_len, size_t pad_beg, size_t pad_end) {
    size_t total_add = src_len + pad_beg + pad_end;
    size_t total_del = del_end > del_beg ? del_end - del_beg : 0;
    size_t tail = line->len > del_end ? line->len - del_end : 0;
    if (total_add > (size_t)(-1) - (line->len - total_del)) {
        return false;
    }
    size_t need = line->len - total_del + total_add;
    if (need > line->cap) {
        size_t grown = line->cap != 0 ? line->cap : 64;
        while (grown < need) {
            if (grown > (size_t)(-1) / 2) {
                grown = need;
                break;
            }
            grown *= 2;
        }
        char *nb = (char *)realloc(line->data, grown);
        if (nb == NULL) {
            return false;
        }
        line->data = nb;
        line->cap = grown;
    }
    if (total_add != total_del) {
        memmove(line->data + del_beg + total_add, line->data + del_end, tail);
    }
    char *p = line->data + del_beg;
    memset(p, ' ', pad_beg);
    p += pad_beg;
    if (src_len > 0) {
        memcpy(p, src, src_len);
        p += src_len;
    }
    memset(p, ' ', pad_end);
    line->len = need;
    return true;
}

static bool out_append(fb_out_t *out, const void *src, size_t n) {
    if (n == 0) {
        return true;
    }
    if (src == NULL) {
        return false;
    }
    if (n > (size_t)(-1) - out->len) {
        return false;
    }
    if (out->len + n > out->cap) {
        size_t grown = out->cap != 0 ? out->cap : 256;
        while (grown < out->len + n) {
            if (grown > (size_t)(-1) / 2) {
                grown = out->len + n;
                break;
            }
            grown *= 2;
        }
        char *nb = (char *)realloc(out->data, grown);
        if (nb == NULL) {
            return false;
        }
        out->data = nb;
        out->cap = grown;
    }
    memcpy(out->data + out->len, src, n);
    out->len += n;
    return true;
}

static bool out_fmt(fb_out_t *out, const char *fmt, ...) {
    char stack[128];
    va_list ap;
    va_start(ap, fmt);
    va_list aq;
    va_copy(aq, ap);
    int n = vsnprintf(stack, sizeof stack, fmt, ap);
    va_end(ap);
    if (n < 0) {
        va_end(aq);
        return false;
    }
    if ((size_t)n < sizeof stack) {
        va_end(aq);
        return out_append(out, stack, (size_t)n);
    }
    char *heap = (char *)malloc((size_t)n + 1);
    if (heap == NULL) {
        va_end(aq);
        return false;
    }
    vsnprintf(heap, (size_t)n + 1, fmt, aq);
    va_end(aq);
    bool ok = out_append(out, heap, (size_t)n);
    free(heap);
    return ok;
}

int edit_fb_init(edit_fb_t *f) {
    if (f == NULL) {
        return -1;
    }
    memset(f, 0, sizeof *f);
    struct fb_impl *impl = (struct fb_impl *)calloc(1, sizeof *impl);
    if (impl == NULL) {
        return -1;
    }
    f->impl = impl;
    memcpy(f->indexed, EDIT_FB_DEFAULT_THEME, sizeof EDIT_FB_DEFAULT_THEME);
    f->auto_colors[0] = EDIT_FB_DEFAULT_THEME[EDIT_FB_BLACK];
    f->auto_colors[1] = EDIT_FB_DEFAULT_THEME[EDIT_FB_WHITE];
    f->background_fill = EDIT_FB_DEFAULT_THEME[EDIT_FB_BACKGROUND];
    f->foreground_fill = EDIT_FB_DEFAULT_THEME[EDIT_FB_FOREGROUND];
    return 0;
}

static void buffer_free(struct edit_fb_buffer *b) {
    if (b->lines != NULL) {
        for (size_t i = 0; i < b->nlines; ++i) {
            free(b->lines[i].data);
        }
        free(b->lines);
        b->lines = NULL;
    }
    free(b->bg.data);
    free(b->fg.data);
    free(b->attrs.data);
    b->bg.data = NULL;
    b->fg.data = NULL;
    b->attrs.data = NULL;
    b->nlines = 0;
    b->bg.count = 0;
    b->fg.count = 0;
    b->attrs.count = 0;
}

void edit_fb_destroy(edit_fb_t *f) {
    if (f == NULL || f->impl == NULL) {
        return;
    }
    struct fb_impl *impl = impl_of(f);
    buffer_free(&impl->buffers[0]);
    buffer_free(&impl->buffers[1]);
    free(impl->out.data);
    free(impl);
    memset(f, 0, sizeof *f);
}

static bool is_dark(uint32_t color) {
    return edit_srgb_to_oklab(color).l < 0.5f;
}

void edit_fb_set_theme(edit_fb_t *f, const uint32_t colors[EDIT_FB_COLORS]) {
    if (f == NULL || colors == NULL) {
        return;
    }
    memcpy(f->indexed, colors, sizeof f->indexed);
    f->background_fill = 0;
    f->foreground_fill = 0;
    memset(f->contrast_valid, 0, sizeof f->contrast_valid);
    f->auto_colors[0] = colors[EDIT_FB_BLACK];
    f->auto_colors[1] = colors[EDIT_FB_BRIGHT_WHITE];
    if (!is_dark(f->auto_colors[0])) {
        uint32_t tmp = f->auto_colors[0];
        f->auto_colors[0] = f->auto_colors[1];
        f->auto_colors[1] = tmp;
    }
}

uint32_t edit_fb_indexed(const edit_fb_t *f, edit_fb_color_t index) {
    if (f == NULL || (unsigned)index >= EDIT_FB_COLORS) {
        return 0;
    }
    return f->indexed[index];
}

uint32_t edit_fb_indexed_alpha(const edit_fb_t *f, edit_fb_color_t index, uint32_t num,
                               uint32_t den) {
    if (f == NULL || (unsigned)index >= EDIT_FB_COLORS || den == 0) {
        return 0;
    }
    uint32_t c = f->indexed[index];
    uint32_t a = 255 * num / den;
    uint32_t r = (((c >> 16) & 0xFF) * num) / den;
    uint32_t g = (((c >> 8) & 0xFF) * num) / den;
    uint32_t b = ((c & 0xFF) * num) / den;
    return (a << 24) | (r << 16) | (g << 8) | b;
}

uint32_t edit_fb_contrasted(edit_fb_t *f, uint32_t color) {
    if (f == NULL) {
        return 0;
    }
    size_t idx = (size_t)(((uint64_t)color * FB_HASH_MULT) >> FB_CACHE_SHIFT);
    if (idx >= 256) {
        idx = 255;
    }
    if (f->contrast_valid[idx] && f->contrast_cache[idx][0] == color) {
        return f->contrast_cache[idx][1];
    }
    uint32_t contrast = f->auto_colors[is_dark(color) ? 1 : 0];
    f->contrast_cache[idx][0] = color;
    f->contrast_cache[idx][1] = contrast;
    f->contrast_valid[idx] = true;
    return contrast;
}

// Clamp to the non-negative viewport box (hardens Rust's as-usize casts).
static bool clamp_box(edit_size_t size, edit_rect_t *r) {
    r->left = r->left < 0 ? 0 : r->left;
    r->top = r->top < 0 ? 0 : r->top;
    r->right = r->right > size.width ? size.width : r->right;
    r->bottom = r->bottom > size.height ? size.height : r->bottom;
    if (r->left > r->right) {
        r->right = r->left;
    }
    if (r->top > r->bottom) {
        r->bottom = r->top;
    }
    return r->left < r->right && r->top < r->bottom;
}

static size_t area_cells(edit_size_t size) {
    if (size.width <= 0 || size.height <= 0) {
        return 0;
    }
    size_t w = (size_t)size.width;
    size_t h = (size_t)size.height;
    if (w > (size_t)(-1) / h) {
        return (size_t)(-1);
    }
    return w * h;
}

// (Re)allocates a buffer for size; false on OOM. Preserves nothing.
static bool buffer_alloc(struct edit_fb_buffer *b, edit_size_t size) {
    size_t cells = area_cells(size);
    if (cells == (size_t)(-1)) {
        return false;
    }
    size_t height = size.height > 0 ? (size_t)size.height : 0;
    fb_line_t *lines = NULL;
    if (height > 0) {
        lines = (fb_line_t *)calloc(height, sizeof *lines);
        if (lines == NULL) {
            return false;
        }
    }
    uint32_t *bg = NULL;
    uint32_t *fg = NULL;
    uint8_t *at = NULL;
    if (cells > 0) {
        bg = (uint32_t *)calloc(cells, sizeof *bg);
        fg = (uint32_t *)calloc(cells, sizeof *fg);
        at = (uint8_t *)calloc(cells, sizeof *at);
        if (bg == NULL || fg == NULL || at == NULL) {
            free(lines);
            free(bg);
            free(fg);
            free(at);
            return false;
        }
    }
    buffer_free(b);
    b->lines = lines;
    b->nlines = height;
    b->size = size;
    b->bg.data = bg;
    b->bg.count = cells;
    b->bg.size = size;
    b->fg.data = fg;
    b->fg.count = cells;
    b->fg.size = size;
    b->attrs.data = at;
    b->attrs.count = cells;
    b->attrs.size = size;
    return true;
}

int edit_fb_flip(edit_fb_t *f, edit_size_t size) {
    if (f == NULL || f->impl == NULL) {
        return -1;
    }
    struct fb_impl *impl = impl_of(f);
    if (size.width != impl->buffers[0].size.width || size.height != impl->buffers[0].size.height) {
        if (!buffer_alloc(&impl->buffers[0], size) || !buffer_alloc(&impl->buffers[1], size)) {
            return -1;
        }
        struct edit_fb_buffer *front = &impl->buffers[f->frame & 1];
        if (front->fg.count > 0) {
            edit_memset_u32(front->fg.data, 1, front->fg.count);
        }
        front->cursor.x = INT32_MIN;
        front->cursor.y = INT32_MIN;
        front->cursor.overtype = false;
        front->cursor.disabled = false;
    }
    f->frame += 1;
    struct edit_fb_buffer *back = &impl->buffers[f->frame & 1];
    // Fill the back buffer with whitespace + fills.
    for (size_t i = 0; i < back->nlines; ++i) {
        fb_line_t *line = &back->lines[i];
        size_t width = size.width > 0 ? (size_t)size.width : 0;
        size_t want = width + width / 2;
        if (want > line->cap) {
            char *nb = (char *)realloc(line->data, want > 0 ? want : 1);
            if (nb == NULL) {
                return -1;
            }
            line->data = nb;
            line->cap = want;
        }
        if (width > 0) {
            memset(line->data, ' ', width);
        }
        line->len = width;
    }
    if (back->bg.count > 0) {
        edit_memset_u32(back->bg.data, f->background_fill, back->bg.count);
    }
    if (back->fg.count > 0) {
        edit_memset_u32(back->fg.data, f->foreground_fill, back->fg.count);
    }
    if (back->attrs.count > 0) {
        memset(back->attrs.data, 0, back->attrs.count);
    }
    back->cursor.x = -1;
    back->cursor.y = -1;
    back->cursor.overtype = false;
    back->cursor.disabled = true;
    return 0;
}

// Slice-doc helpers for measurement over a byte range.
typedef struct {
    edit_doc_t doc;
    const uint8_t *bytes;
    size_t len;
} fb_slice_t;

static size_t fb_slice_len(const void *ctx) {
    const fb_slice_t *s = (const fb_slice_t *)ctx;
    return s->len;
}

static void fb_slice_fwd(const void *ctx, size_t off, const uint8_t **p, size_t *n) {
    const fb_slice_t *s = (const fb_slice_t *)ctx;
    size_t o = off < s->len ? off : s->len;
    *p = s->bytes + o;
    *n = s->len - o;
}

static void fb_measure(const uint8_t *bytes, size_t len, edit_measure_t *m, edit_doc_t *doc,
                       fb_slice_t *slice) {
    slice->bytes = bytes;
    slice->len = len;
    slice->doc.ctx = slice;
    slice->doc.len = fb_slice_len;
    slice->doc.read_fwd = fb_slice_fwd;
    slice->doc.read_bwd = NULL;
    slice->doc.replace = NULL;
    edit_measure_init(m, &slice->doc);
    *doc = slice->doc;
}

int edit_fb_replace_text(edit_fb_t *f, int32_t y, int32_t origin_x, int32_t clip_right,
                         const char *text, size_t len) {
    if (f == NULL || f->impl == NULL) {
        return -1;
    }
    if (text == NULL) {
        text = "";
    }
    struct edit_fb_buffer *back = &impl_of(f)->buffers[f->frame & 1];
    if (y < 0 || (size_t)y >= back->nlines) {
        return 0;
    }
    fb_line_t *line = &back->lines[y];
    int32_t width = back->size.width;
    int32_t clip = clip_right < 0 ? 0 : clip_right > width ? width : clip_right;
    int64_t layout64 = (int64_t)clip - (int64_t)origin_x;
    if (layout64 <= 0 || layout64 > INT32_MAX || len == 0) {
        return 0;
    }
    int32_t layout = (int32_t)layout64;

    fb_slice_t slice;
    edit_measure_t m;
    edit_doc_t doc;
    fb_measure((const uint8_t *)text, len, &m, &doc, &slice);

    int32_t left = origin_x;
    if (left < 0) {
        int64_t need64 = -(int64_t)left;
        int32_t need = need64 > INT32_MAX ? INT32_MAX : (int32_t)need64;
        edit_cursor_t cur = edit_measure_goto_visual(&m, (edit_point_t){need, 0});
        if (left + cur.visual.x < 0 && cur.offset < len) {
            cur = edit_measure_goto_logical(&m, (edit_point_t){cur.logical.x + 1, 0});
        }
        left += cur.visual.x;
    }
    if (left < 0 || left >= clip) {
        return 0;
    }

    size_t beg_off = edit_measure_cursor(&m).offset;
    edit_cursor_t fin = edit_measure_goto_visual(&m, (edit_point_t){layout, 0});
    int32_t right = left + fin.visual.x;

    // Old-text span to delete (with wide-glyph overshoot).
    fb_slice_t lslice;
    edit_measure_t lm;
    edit_doc_t ldoc;
    fb_measure((const uint8_t *)line->data, line->len, &lm, &ldoc, &lslice);
    edit_cursor_t old_beg = edit_measure_goto_visual(&lm, (edit_point_t){left, 0});
    edit_cursor_t old_end = edit_measure_goto_visual(&lm, (edit_point_t){right, 0});
    if (old_end.visual.x < right) {
        old_end = edit_measure_goto_logical(&lm, (edit_point_t){old_end.logical.x + 1, 0});
    }
    size_t src_len = fin.offset - beg_off;
    int32_t pad_beg = left - old_beg.visual.x;
    if (pad_beg < 0) {
        pad_beg = 0;
    }
    int32_t pad_end = old_end.visual.x - right;
    if (pad_end < 0) {
        pad_end = 0;
    }
    if (!line_splice(line, old_beg.offset, old_end.offset, (const uint8_t *)text + beg_off, src_len,
                     (size_t)pad_beg, (size_t)pad_end)) {
        return -1;
    }
    return 0;
}

static void bitmap_blend(fb_bitmap_t *bm, edit_rect_t target, uint32_t color) {
    if ((color & 0xFF000000U) == 0) {
        return;
    }
    edit_rect_t box = target;
    if (!clamp_box(bm->size, &box)) {
        return;
    }
    size_t top = (size_t)box.top;
    size_t bottom = (size_t)box.bottom;
    size_t left = (size_t)box.left;
    size_t right = (size_t)box.right;
    size_t stride = (size_t)bm->size.width;
    for (size_t y = top; y < bottom; ++y) {
        size_t beg = y * stride + left;
        size_t end = y * stride + right;
        if (end > bm->count) {
            break;
        }
        if ((color & 0xFF000000U) == 0xFF000000U) {
            edit_memset_u32(bm->data + beg, color, end - beg);
        } else {
            // Run-length blend so oklab_blend runs once per run.
            size_t off = beg;
            while (off < end) {
                uint32_t c = bm->data[off];
                size_t run = off + 1;
                while (run < end && bm->data[run] == c) {
                    ++run;
                }
                uint32_t blended = edit_oklab_blend(c, color);
                edit_memset_u32(bm->data + off, blended, run - off);
                off = run;
            }
        }
    }
}

void edit_fb_blend_bg(edit_fb_t *f, edit_rect_t target, uint32_t bg) {
    if (f == NULL || f->impl == NULL) {
        return;
    }
    bitmap_blend(&impl_of(f)->buffers[f->frame & 1].bg, target, bg);
}

void edit_fb_blend_fg(edit_fb_t *f, edit_rect_t target, uint32_t fg) {
    if (f == NULL || f->impl == NULL) {
        return;
    }
    bitmap_blend(&impl_of(f)->buffers[f->frame & 1].fg, target, fg);
}

void edit_fb_reverse(edit_fb_t *f, edit_rect_t target) {
    if (f == NULL || f->impl == NULL) {
        return;
    }
    struct edit_fb_buffer *back = &impl_of(f)->buffers[f->frame & 1];
    edit_rect_t box = target;
    if (!clamp_box(back->bg.size, &box)) {
        return;
    }
    size_t stride = (size_t)back->bg.size.width;
    for (int32_t y = box.top; y < box.bottom; ++y) {
        size_t beg = (size_t)y * stride + (size_t)box.left;
        size_t end = (size_t)y * stride + (size_t)box.right;
        if (end > back->bg.count || end > back->fg.count) {
            break;
        }
        for (size_t i = beg; i < end; ++i) {
            uint32_t tmp = back->bg.data[i];
            back->bg.data[i] = back->fg.data[i];
            back->fg.data[i] = tmp;
        }
    }
}

void edit_fb_replace_attr(edit_fb_t *f, edit_rect_t target, edit_fb_attr_t mask,
                          edit_fb_attr_t attr) {
    if (f == NULL || f->impl == NULL) {
        return;
    }
    struct edit_fb_buffer *back = &impl_of(f)->buffers[f->frame & 1];
    edit_rect_t box = target;
    if (!clamp_box(back->attrs.size, &box)) {
        return;
    }
    size_t stride = (size_t)back->attrs.size.width;
    for (int32_t y = box.top; y < box.bottom; ++y) {
        size_t beg = (size_t)y * stride + (size_t)box.left;
        size_t end = (size_t)y * stride + (size_t)box.right;
        if (end > back->attrs.count) {
            break;
        }
        if (mask == EDIT_FB_ATTR_ALL) {
            memset(back->attrs.data + beg, attr, end - beg);
        } else {
            for (size_t i = beg; i < end; ++i) {
                back->attrs.data[i] = (uint8_t)((back->attrs.data[i] & (uint8_t)~mask) | attr);
            }
        }
    }
}

void edit_fb_set_cursor(edit_fb_t *f, edit_point_t pos, bool overtype) {
    if (f == NULL || f->impl == NULL) {
        return;
    }
    fb_cursor_t *c = &impl_of(f)->buffers[f->frame & 1].cursor;
    c->x = pos.x;
    c->y = pos.y;
    c->overtype = overtype;
    c->disabled = false;
}

int32_t edit_fb_scrollbar(edit_fb_t *f, edit_rect_t clip, edit_rect_t track, int32_t content_offset,
                          int32_t content_height) {
    if (f == NULL) {
        return 0;
    }
    edit_rect_t tc = track;
    tc.left = tc.left > clip.left ? tc.left : clip.left;
    tc.top = tc.top > clip.top ? tc.top : clip.top;
    tc.right = tc.right < clip.right ? tc.right : clip.right;
    tc.bottom = tc.bottom < clip.bottom ? tc.bottom : clip.bottom;
    if (tc.left >= tc.right || tc.top >= tc.bottom) {
        return 0;
    }
    int32_t viewport_h = tc.bottom - tc.top;
    if (content_height < viewport_h) {
        content_height = viewport_h;
    }
    int32_t offset_max = content_height - viewport_h;
    if (offset_max == 0) {
        return 0;
    }
    int32_t coff = content_offset < 0            ? 0
                   : content_offset > offset_max ? offset_max
                                                 : content_offset;

    int64_t vh = (int64_t)viewport_h * 8;
    int64_t omax = (int64_t)offset_max * 8;
    int64_t coff8 = (int64_t)coff * 8;
    int64_t ch = (int64_t)content_height * 8;

    int64_t thumb_h = (vh * vh + ch / 2) / ch;
    if (thumb_h < 8) {
        thumb_h = 8;
    }
    int64_t thumb_top = ((vh - thumb_h) * coff8 + omax / 2) / omax;
    int64_t thumb_bottom = thumb_top + thumb_h;

    thumb_top += (int64_t)track.top * 8;
    thumb_bottom += (int64_t)track.top * 8;
    int64_t clip_top = (int64_t)tc.top * 8;
    int64_t clip_bottom = (int64_t)tc.bottom * 8;
    if (thumb_top < clip_top) {
        thumb_top = clip_top;
    }
    if (thumb_bottom > clip_bottom) {
        thumb_bottom = clip_bottom;
    }

    int32_t top_fract = (int32_t)(thumb_top % 8);
    int32_t bottom_fract = (int32_t)(thumb_bottom % 8);
    int32_t thumb_top_r = (int32_t)((thumb_top + 7) / 8);
    int32_t thumb_bottom_r = (int32_t)(thumb_bottom / 8);

    edit_fb_blend_bg(f, tc, edit_fb_indexed(f, EDIT_FB_BRIGHT_BLACK));
    edit_fb_blend_fg(f, tc, edit_fb_indexed(f, EDIT_FB_BRIGHT_WHITE));

    for (int32_t y = thumb_top_r; y < thumb_bottom_r; ++y) {
        edit_fb_replace_text(f, y, tc.left, tc.right, "\xE2\x96\x88", 3);
    }

    char fract[3] = {(char)0xE2, (char)0x96, (char)0x88};
    if (top_fract != 0) {
        fract[2] = (char)(0x88 - top_fract);
        edit_fb_replace_text(f, thumb_top_r - 1, tc.left, tc.right, fract, 3);
    }
    if (bottom_fract != 0) {
        fract[2] = (char)(0x88 - bottom_fract);
        edit_fb_replace_text(f, thumb_bottom_r, tc.left, tc.right, fract, 3);
        edit_rect_t cell = {tc.left, thumb_bottom_r, tc.right, thumb_bottom_r + 1};
        edit_fb_blend_bg(f, cell, edit_fb_indexed(f, EDIT_FB_BRIGHT_WHITE));
        edit_fb_blend_fg(f, cell, edit_fb_indexed(f, EDIT_FB_BRIGHT_BLACK));
    }

    return (int32_t)((thumb_h + 4) / 8);
}

static bool format_color(edit_fb_t *f, fb_out_t *out, bool fg, uint32_t color) {
    char typ = fg ? '3' : '4';
    if (color == 0) {
        return out_fmt(out, "\x1b[%c9m", typ);
    }
    if ((color & 0xFF000000U) != 0xFF000000U) {
        uint32_t dst = edit_fb_indexed(f, fg ? EDIT_FB_FOREGROUND : EDIT_FB_BACKGROUND);
        color = edit_oklab_blend(dst, color);
    }
    unsigned r = color & 0xFFU;
    unsigned g = (color >> 8U) & 0xFFU;
    unsigned b = (color >> 16U) & 0xFFU;
    return out_fmt(out, "\x1b[%c8;2;%u;%u;%um", typ, r, g, b);
}

size_t edit_fb_render(edit_fb_t *f, const char **out) {
    if (out != NULL) {
        *out = NULL;
    }
    if (f == NULL || f->impl == NULL || out == NULL) {
        return 0;
    }
    struct fb_impl *impl = impl_of(f);
    struct edit_fb_buffer *back = &impl->buffers[f->frame & 1];
    struct edit_fb_buffer *front = &impl->buffers[(f->frame & 1) ^ 1];
    fb_out_t *result = &impl->out;
    result->len = 0;

    uint64_t last_bg = UINT64_MAX;
    uint64_t last_fg = UINT64_MAX;
    uint8_t last_attr = EDIT_FB_ATTR_NONE;

    int32_t height = back->size.height;
    for (int32_t y = 0; y < height; ++y) {
        size_t i = (size_t)y;
        if (i >= back->nlines || i >= front->nlines) {
            break;
        }
        fb_line_t *fline = &front->lines[i];
        fb_line_t *bline = &back->lines[i];
        size_t width = back->bg.size.width > 0 ? (size_t)back->bg.size.width : 0;
        size_t row = (size_t)y * width;
        bool same =
            row + width <= back->bg.count && row + width <= front->bg.count &&
            row + width <= back->fg.count && row + width <= front->fg.count &&
            row + width <= back->attrs.count && row + width <= front->attrs.count &&
            fline->len == bline->len && memcmp(fline->data, bline->data, fline->len) == 0 &&
            memcmp(front->bg.data + row, back->bg.data + row, width * sizeof(uint32_t)) == 0 &&
            memcmp(front->fg.data + row, back->fg.data + row, width * sizeof(uint32_t)) == 0 &&
            memcmp(front->attrs.data + row, back->attrs.data + row, width) == 0;
        if (same) {
            continue;
        }

        if (result->len == 0) {
            if (!out_append(result, "\x1b[m", 3)) {
                break;
            }
        }
        if (!out_fmt(result, "\x1b[%d;1H", y + 1)) {
            break;
        }

        // Chunk the row into same-color runs; map visual runs to bytes.
        fb_slice_t slice;
        edit_measure_t m;
        edit_doc_t doc;
        fb_measure((const uint8_t *)bline->data, bline->len, &m, &doc, &slice);
        size_t chunk_end = 0;
        bool row_ok = true;
        while (chunk_end < width && row_ok) {
            if (row + chunk_end >= back->bg.count) {
                break;
            }
            uint32_t bg = back->bg.data[row + chunk_end];
            uint32_t fg = back->fg.data[row + chunk_end];
            uint8_t attr = back->attrs.data[row + chunk_end];
            size_t run = chunk_end + 1;
            while (run < width && row + run < back->bg.count && back->bg.data[row + run] == bg &&
                   back->fg.data[row + run] == fg && back->attrs.data[row + run] == attr) {
                ++run;
            }
            if (last_bg != bg) {
                last_bg = bg;
                row_ok = format_color(f, result, false, bg);
            }
            if (row_ok && last_fg != fg) {
                last_fg = fg;
                row_ok = format_color(f, result, true, fg);
            }
            if (row_ok && last_attr != attr) {
                uint8_t diff = (uint8_t)(last_attr ^ attr);
                if ((diff & EDIT_FB_ATTR_ITALIC) != 0) {
                    bool on = ((attr & EDIT_FB_ATTR_ITALIC) != 0);
                    row_ok = out_append(result, on ? "\x1b[3m" : "\x1b[23m", on ? 4 : 5);
                }
                if (row_ok && (diff & EDIT_FB_ATTR_UNDERLINED) != 0) {
                    bool on = ((attr & EDIT_FB_ATTR_UNDERLINED) != 0);
                    row_ok = out_append(result, on ? "\x1b[4m" : "\x1b[24m", on ? 4 : 5);
                }
                last_attr = attr;
            }
            if (row_ok) {
                size_t beg = edit_measure_cursor(&m).offset;
                size_t run_cols = run > (size_t)INT32_MAX ? (size_t)INT32_MAX : run;
                edit_cursor_t end =
                    edit_measure_goto_visual(&m, (edit_point_t){(int32_t)run_cols, 0});
                if (end.offset > beg && beg < bline->len) {
                    size_t n = end.offset - beg;
                    if (n > bline->len - beg) {
                        n = bline->len - beg;
                    }
                    row_ok = out_append(result, bline->data + beg, n);
                }
            }
            chunk_end = run;
        }
        if (!row_ok) {
            break;
        }
    }

    // Cursor update when the output moved it or it changed.
    bool cursor_diff = back->cursor.x != front->cursor.x || back->cursor.y != front->cursor.y ||
                       back->cursor.overtype != front->cursor.overtype ||
                       back->cursor.disabled != front->cursor.disabled;
    if (result->len > 0 || cursor_diff) {
        if (!back->cursor.disabled && back->cursor.x >= 0 && back->cursor.y >= 0) {
            if (!out_fmt(result, "\x1b[%d;%dH\x1b[%d q\x1b[?25h", back->cursor.y + 1,
                         back->cursor.x + 1, back->cursor.overtype ? 1 : 5)) {
                // best effort
            }
        } else {
            out_append(result, "\x1b[?25l", 6);
        }
    }
    *out = result->data;
    return result->len;
}
