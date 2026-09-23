#include "edit/tbuf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "edit/fb.h"
#include "edit/icu.h"
#include "edit/nav.h"
#include "edit/oklab.h"
#include "edit/utf8.h"
#include "tbuf_priv.h"

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} render_line_t;

static bool line_reserve(render_line_t *l, size_t additional) {
    if (additional > (size_t)-1 - l->len) {
        return false;
    }
    size_t need = l->len + additional;
    if (need > l->cap) {
        size_t grown = l->cap != 0 ? l->cap : 64;
        while (grown < need) {
            if (grown > (size_t)-1 / 2) {
                grown = need;
                break;
            }
            grown *= 2;
        }
        char *nb = (char *)realloc(l->data, grown);
        if (nb == NULL) {
            return false;
        }
        l->data = nb;
        l->cap = grown;
    }
    return true;
}

static bool line_append(render_line_t *l, const void *src, size_t n) {
    if (n == 0) {
        return true;
    }
    if (src == NULL || !line_reserve(l, n)) {
        return false;
    }
    memcpy(l->data + l->len, src, n);
    l->len += n;
    return true;
}

static int64_t abs64(int32_t a, int32_t b) {
    int64_t d = (int64_t)a - (int64_t)b;
    return d < 0 ? -d : d;
}

static const char SPACES_20[] = "                    ";
static const char MARGIN_TAIL[] = "\xE2\x94\x82 "; // │ + space

bool edit_tbuf_render(edit_tbuf_t *t, edit_point_t origin, edit_rect_t destination, bool focused,
                      edit_fb_t *fb, int32_t *out_xmax) {
    if (out_xmax != NULL) {
        *out_xmax = 0;
    }
    if (t == NULL || fb == NULL) {
        return false;
    }
    if (destination.left >= destination.right || destination.top >= destination.bottom) {
        return false;
    }

    int32_t width = destination.right - destination.left;
    int32_t height = destination.bottom - destination.top;
    int32_t margin_w = t->margin_width > 3 ? t->margin_width : 3;
    size_t number_width = (size_t)(margin_w - 3);
    int32_t text_width = width - t->margin_width;
    render_line_t line = {NULL, 0, 0};
    int32_t xmax = 0;
    bool ok = true;

    edit_cursor_t cursor = t->cursor;
    if (t->has_render_cursor) {
        int64_t da = abs64(t->cursor.visual.y, origin.y);
        int64_t db = abs64(t->render_cursor.visual.y, origin.y);
        if (!(da < db)) {
            cursor = t->render_cursor;
        }
    }

    edit_point_t sel_beg = {INT32_MIN, INT32_MIN};
    edit_point_t sel_end = {INT32_MIN, INT32_MIN};
    if (t->has_selection) {
        sel_beg = t->sel_beg;
        sel_end = t->sel_end;
        if (edit_point_cmp(sel_beg, sel_end) > 0) {
            edit_point_t tmp = sel_beg;
            sel_beg = sel_end;
            sel_end = tmp;
        }
    }

    if (width > 0 && !line_reserve(&line, (size_t)width * 2)) {
        return false;
    }

    for (int32_t y = 0; y < height && ok; ++y) {
        line.len = 0;
        int32_t visual_line = origin.y + y;
        edit_cursor_t cbeg = tbuf_move_to_visual(t, cursor, (edit_point_t){origin.x, visual_line});
        edit_point_t cend_pt = {origin.x + text_width, visual_line};
        edit_cursor_t cend = tbuf_move_to_visual(t, cbeg, cend_pt);

        if (y == 0) {
            t->has_render_cursor = true;
            t->render_cursor = cbeg;
        }

        if (number_width != 0) {
            if (visual_line >= t->visual_lines) {
                size_t spaces = number_width + 1;
                if (spaces > 20) {
                    spaces = 20;
                }
                // Past-end margin: spaces then │ + space.
                ok = line_append(&line, SPACES_20, spaces) && line_append(&line, MARGIN_TAIL, 4);
            } else if (t->wrap_col <= 0 || cbeg.logical.x == 0) {
                char numbuf[32];
                int n = snprintf(numbuf, sizeof numbuf, "%*d \xE2\x94\x82 ", (int)number_width,
                                 cbeg.logical.y + 1);
                if (n < 0 || !line_append(&line, numbuf, (size_t)n)) {
                    ok = false;
                }
            } else {
                int32_t digits = 1;
                int32_t v = cbeg.logical.y + 1;
                while (v >= 10) {
                    v /= 10;
                    digits += 1;
                }
                for (int32_t i = 0; i < (int32_t)number_width - digits; ++i) {
                    if (!line_append(&line, " ", 1)) {
                        ok = false;
                        break;
                    }
                }
                for (int32_t i = 0; ok && i < digits; ++i) {
                    if (!line_append(&line, "\xE2\x88\x99", 3)) {
                        ok = false;
                    }
                }
                if (ok) {
                    ok = line_append(&line, " \xE2\x94\x82 ", 5);
                }
                if (ok) {
                    int32_t left = destination.left;
                    int32_t top = destination.top + y;
                    edit_rect_t r = {left, top, left + (int32_t)number_width, top + 1};
                    edit_fb_blend_fg(fb, r, edit_fb_indexed_alpha(fb, EDIT_FB_BACKGROUND, 1, 2));
                }
            }
        }

        if (ok && cbeg.offset != cend.offset) {
            if (cbeg.visual.x < origin.x) {
                edit_point_t np = {cbeg.logical.x + 1, cbeg.logical.y};
                edit_cursor_t next = tbuf_move_to_logical(t, cbeg, np);
                if (next.visual.x > origin.x) {
                    int32_t overlap = next.visual.x - origin.x;
                    if (overlap >= 1 && overlap <= 7) {
                        ok = line_append(&line, SPACES_20, (size_t)overlap);
                        cbeg = next;
                    }
                }
            }
        }

        if (ok && cbeg.offset != cend.offset) {
            size_t global_off = cbeg.offset;
            edit_cursor_t cursor_tab = cbeg;
            uint8_t visbuf[3] = {0xE2, 0x90, 0x80};
            while (ok && global_off < cend.offset) {
                const uint8_t *chunk = NULL;
                size_t chunk_len = 0;
                edit_tbuf_read_fwd(t, global_off, &chunk, &chunk_len);
                if (chunk == NULL || chunk_len == 0) {
                    break;
                }
                size_t remain = cend.offset - global_off;
                if (chunk_len > remain) {
                    chunk_len = remain;
                }
                size_t chunk_off = 0;
                while (ok && chunk_off < chunk_len) {
                    size_t beg = chunk_off;
                    while (chunk_off < chunk_len && chunk[chunk_off] >= 0x20 &&
                           chunk[chunk_off] != 0x7F) {
                        chunk_off += 1;
                    }
                    // Valid runs pass through; errors become FFFD.
                    // (Decoder grouping matches utf8_chunks per lossy parity,
                    // and literal U+FFFD re-encodes identically.)
                    {
                        edit_utf8_chars_t it;
                        edit_utf8_chars_init(&it, chunk + beg, chunk_off - beg, 0);
                        size_t seg = beg;
                        uint32_t cp = 0;
                        while (ok && edit_utf8_next(&it, &cp)) {
                            size_t cur = beg + edit_utf8_offset(&it);
                            if (cp == 0xFFFDU) {
                                ok = line_append(&line, "\xEF\xBF\xBD", 3);
                            } else if (cur > seg) {
                                ok = line_append(&line, chunk + seg, cur - seg);
                            }
                            seg = cur;
                        }
                    }
                    while (ok && chunk_off < chunk_len &&
                           (chunk[chunk_off] < 0x20 || chunk[chunk_off] == 0x7F)) {
                        uint8_t ch = chunk[chunk_off];
                        chunk_off += 1;
                        if (ch == '\t') {
                            cursor_tab =
                                tbuf_move_to_offset(t, cursor_tab, global_off + chunk_off - 1);
                            int32_t spaces = t->tab_size - (cursor_tab.column % t->tab_size);
                            if (spaces < 0) {
                                spaces = 0;
                            }
                            if (spaces > 20) {
                                spaces = 20;
                            }
                            ok = line_append(&line, SPACES_20, (size_t)spaces);
                            while (ok && chunk_off < chunk_len && chunk[chunk_off] == '\t') {
                                int32_t ts = t->tab_size > 20 ? 20 : t->tab_size;
                                if (ts < 0) {
                                    ts = 0;
                                }
                                ok = line_append(&line, SPACES_20, (size_t)ts);
                                chunk_off += 1;
                            }
                            continue;
                        }
                        visbuf[2] = ch == 0x7F ? (uint8_t)0xA1 : (uint8_t)(0x80U | ch);
                        ok = line_append(&line, visbuf, 3);
                    }
                }
                global_off += chunk_len;
            }
            if (cend.visual.x > xmax) {
                xmax = cend.visual.x;
            }
        }

        if (ok) {
            ok = edit_fb_replace_text(fb, destination.top + y, destination.left, destination.right,
                                      line.data == NULL ? "" : line.data, line.len) == 0;
        }

        if (ok && cbeg.visual.y == visual_line && edit_point_cmp(sel_beg, cend.logical) <= 0 &&
            edit_point_cmp(sel_end, cbeg.logical) >= 0) {
            int32_t beg = 0;
            int32_t end = EDIT_COORD_SAFE_MAX;
            edit_cursor_t cur = cbeg;
            if (edit_point_cmp(sel_beg, cend.logical) <= 0 &&
                edit_point_cmp(sel_beg, cbeg.logical) >= 0) {
                cur = tbuf_move_to_logical(t, cur, sel_beg);
                beg = cur.visual.x;
            }
            if (edit_point_cmp(sel_end, cend.logical) <= 0 &&
                edit_point_cmp(sel_end, cbeg.logical) >= 0) {
                cur = tbuf_move_to_logical(t, cur, sel_end);
                end = cur.visual.x;
            }
            if (beg < origin.x) {
                beg = origin.x;
            }
            if (end > origin.x + text_width) {
                end = origin.x + text_width;
            }
            int32_t left = destination.left + t->margin_width - origin.x;
            int32_t top = destination.top + y;
            edit_rect_t rect = {left + beg, top, left + end, top + 1};
            uint32_t bg = edit_oklab_blend(edit_fb_indexed(fb, EDIT_FB_FOREGROUND),
                                           edit_fb_indexed_alpha(fb, EDIT_FB_BRIGHT_BLUE, 1, 2));
            if (!focused) {
                bg = edit_oklab_blend(bg, edit_fb_indexed_alpha(fb, EDIT_FB_BACKGROUND, 1, 2));
            }
            uint32_t fg = edit_fb_contrasted(fb, bg);
            edit_fb_blend_bg(fb, rect, bg);
            edit_fb_blend_fg(fb, rect, fg);
        }

        cursor = cend;
    }

    if (ok && t->margin_width > 0) {
        edit_rect_t margin = {destination.left, destination.top, destination.left + t->margin_width,
                              destination.bottom};
        edit_fb_blend_fg(fb, margin, 0x7F3F3F3F);
    }

    if (ok && t->ruler > 0) {
        int32_t left = destination.left + t->margin_width + t->ruler - origin.x;
        if (left < destination.left + t->margin_width) {
            left = destination.left + t->margin_width;
        }
        if (left < destination.right) {
            edit_rect_t r = {left, destination.top, destination.right, destination.bottom};
            edit_fb_blend_bg(fb, r, edit_fb_indexed_alpha(fb, EDIT_FB_BRIGHT_RED, 1, 4));
        }
    }

    if (ok && focused) {
        int32_t x = t->cursor.visual.x;
        int32_t y = t->cursor.visual.y;
        if (t->wrap_col > 0 && x >= t->wrap_col) {
            x = 0;
            y += 1;
        }
        x += destination.left - origin.x + t->margin_width;
        y += destination.top - origin.y;
        edit_point_t cur = {x, y};
        edit_rect_t text = {destination.left + t->margin_width, destination.top, destination.right,
                            destination.bottom};
        if (edit_rect_contains(text, cur)) {
            edit_fb_set_cursor(fb, cur, t->overtype);
            if (t->line_highlight && edit_point_cmp(sel_beg, sel_end) >= 0) {
                edit_rect_t r = {destination.left, cur.y, destination.right, cur.y + 1};
                edit_fb_blend_bg(fb, r, 0x50282828);
            }
        }
    }

    free(line.data);
    if (out_xmax != NULL) {
        *out_xmax = xmax;
    }
    return ok;
}
