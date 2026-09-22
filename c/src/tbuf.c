#include "edit/tbuf.h"

#include <assert.h>
#include <string.h>

#include "edit/nav.h"

#include "edit/simd.h"
#include "tbuf_priv.h"

void tbuf_measure_cfg(const edit_tbuf_t *t, edit_doc_t *doc, edit_measure_t *m) {
    edit_gap_doc((edit_gap_t *)&t->buffer, doc);
    edit_measure_init(m, doc);
    edit_measure_set_wrap(m, t->wrap_col);
    edit_measure_set_tab(m, t->tab_size);
}

static int32_t digits10(int32_t v) {
    int32_t d = 1;
    while (v >= 10) {
        v /= 10;
        d += 1;
    }
    return d;
}

int edit_tbuf_init(edit_tbuf_t *t, bool small) {
    if (t == NULL) {
        return -1;
    }
    memset(t, 0, sizeof *t);
    if (edit_gap_init(&t->buffer, small) != 0) {
        return -1;
    }
    t->logical_lines = 1;
    t->visual_lines = 1;
    t->tab_size = 4;
    memcpy(t->encoding, "UTF-8", 6);
    return 0;
}

void edit_tbuf_destroy(edit_tbuf_t *t);

size_t edit_tbuf_len(const edit_tbuf_t *t) {
    return t == NULL ? 0 : edit_gap_len(&t->buffer);
}

int32_t edit_tbuf_logical_lines(const edit_tbuf_t *t) {
    return t == NULL ? 0 : t->logical_lines;
}

int32_t edit_tbuf_visual_lines(const edit_tbuf_t *t) {
    return t == NULL ? 0 : t->visual_lines;
}

bool edit_tbuf_is_dirty(const edit_tbuf_t *t) {
    return t != NULL && t->save_generation != edit_gap_generation(&t->buffer);
}

uint32_t edit_tbuf_generation(const edit_tbuf_t *t) {
    return t == NULL ? 0 : edit_gap_generation(&t->buffer);
}

void edit_tbuf_mark_dirty(edit_tbuf_t *t) {
    if (t != NULL) {
        t->save_generation = edit_gap_generation(&t->buffer) - 1;
    }
}

const char *edit_tbuf_encoding(const edit_tbuf_t *t) {
    return t == NULL ? "UTF-8" : t->encoding;
}

void edit_tbuf_set_encoding(edit_tbuf_t *t, const char *encoding) {
    if (t == NULL || encoding == NULL) {
        return;
    }
    if (strcmp(t->encoding, encoding) != 0) {
        size_t n = strlen(encoding);
        if (n > sizeof t->encoding - 1) {
            n = sizeof t->encoding - 1;
        }
        memcpy(t->encoding, encoding, n);
        t->encoding[n] = '\0';
        edit_tbuf_mark_dirty(t);
    }
}

bool edit_tbuf_is_crlf(const edit_tbuf_t *t) {
    return t != NULL && t->newlines_crlf;
}

bool edit_tbuf_is_overtype(const edit_tbuf_t *t) {
    return t != NULL && t->overtype;
}

void edit_tbuf_set_overtype(edit_tbuf_t *t, bool overtype) {
    if (t != NULL) {
        t->overtype = overtype;
    }
}

void tbuf_set_cursor_internal(edit_tbuf_t *t, edit_cursor_t cursor) {
    assert(cursor.offset <= edit_gap_len(&t->buffer) && cursor.logical.x >= 0 &&
           cursor.logical.y >= 0 && cursor.logical.y <= t->logical_lines && cursor.visual.x >= 0 &&
           (t->wrap_col <= 0 || cursor.visual.x <= t->wrap_col) && cursor.visual.y >= 0 &&
           cursor.visual.y <= t->visual_lines);
    t->cursor = cursor;
}

uint32_t tbuf_set_selection(edit_tbuf_t *t, bool has, edit_point_t beg, edit_point_t end) {
    t->has_selection = has;
    t->sel_beg = beg;
    t->sel_end = end;
    t->sel_generation += 1;
    return t->sel_generation;
}

bool edit_tbuf_has_selection(const edit_tbuf_t *t) {
    return t != NULL && t->has_selection;
}

void edit_tbuf_clear_selection(edit_tbuf_t *t) {
    if (t == NULL) {
        return;
    }
    edit_point_t z = {0, 0};
    tbuf_set_selection(t, false, z, z);
}

void edit_tbuf_set_selection(edit_tbuf_t *t, edit_point_t beg, edit_point_t end) {
    if (t == NULL) {
        return;
    }
    tbuf_set_selection(t, true, beg, end);
}

edit_cursor_t tbuf_move_to_offset(const edit_tbuf_t *t, edit_cursor_t cursor, size_t offset);
edit_cursor_t tbuf_move_to_logical(const edit_tbuf_t *t, edit_cursor_t cursor, edit_point_t pos);
edit_cursor_t tbuf_goto_line_start(const edit_tbuf_t *t, edit_cursor_t cursor, int32_t y);

bool edit_tbuf_selection_range(edit_tbuf_t *t, edit_cursor_t *out_beg, edit_cursor_t *out_end) {
    if (out_beg != NULL) {
        memset(out_beg, 0, sizeof *out_beg);
    }
    if (out_end != NULL) {
        memset(out_end, 0, sizeof *out_end);
    }
    if (t == NULL || !t->has_selection) {
        return false;
    }
    edit_point_t pts[2] = {t->sel_beg, t->sel_end};
    if (edit_point_cmp(pts[0], pts[1]) > 0) {
        edit_point_t tmp = pts[0];
        pts[0] = pts[1];
        pts[1] = tmp;
    }
    edit_cursor_t beg = tbuf_move_to_logical(t, t->cursor, pts[0]);
    edit_cursor_t end = tbuf_move_to_logical(t, beg, pts[1]);
    if (beg.offset >= end.offset) {
        return false;
    }
    if (out_beg != NULL) {
        *out_beg = beg;
    }
    if (out_end != NULL) {
        *out_end = end;
    }
    return true;
}

edit_cursor_t tbuf_goto_line_start(const edit_tbuf_t *t, edit_cursor_t cursor, int32_t y) {
    edit_cursor_t result = cursor;
    bool seek_to_start = true;

    if (y > result.logical.y) {
        while (y > result.logical.y) {
            const uint8_t *chunk = NULL;
            size_t chunk_len = 0;
            edit_gap_read_fwd(&t->buffer, result.offset, &chunk, &chunk_len);
            if (chunk == NULL || chunk_len == 0) {
                break;
            }
            size_t delta = 0;
            int32_t line = result.logical.y;
            edit_newlines_forward(chunk, chunk_len, 0, line, y, &delta, &line);
            result.offset += delta;
            result.logical.y = line;
        }
        seek_to_start = result.offset == edit_gap_len(&t->buffer) && result.offset != cursor.offset;
    }

    if (seek_to_start) {
        for (;;) {
            const uint8_t *chunk = NULL;
            size_t chunk_len = 0;
            edit_gap_read_bwd(&t->buffer, result.offset, &chunk, &chunk_len);
            if (chunk == NULL || chunk_len == 0) {
                break;
            }
            size_t delta = 0;
            int32_t line = result.logical.y;
            edit_newlines_backward(chunk, chunk_len, chunk_len, line, y, &delta, &line);
            result.offset -= chunk_len - delta;
            result.logical.y = line;
            if (delta > 0) {
                break;
            }
        }
    }

    if (result.offset == cursor.offset) {
        return result;
    }

    result.logical.x = 0;
    result.visual.x = 0;
    result.visual.y = result.logical.y;
    result.column = 0;
    result.wrap_opp = false;

    if (t->wrap_col > 0) {
        bool upward = result.offset < cursor.offset;
        edit_cursor_t top = upward ? result : cursor;
        edit_cursor_t bottom = upward ? cursor : result;
        edit_doc_t doc;
        edit_measure_t m;
        tbuf_measure_cfg(t, &doc, &m);
        edit_measure_set_cursor(&m, top);
        edit_cursor_t remeasured = edit_measure_goto_logical(&m, bottom.logical);
        if (upward) {
            int32_t a = remeasured.visual.x;
            int32_t b = bottom.visual.x;
            remeasured.visual.y += (a != 0 && b == 0) - (a == 0 && b != 0);
        }
        int32_t delta = remeasured.visual.y - top.visual.y;
        if (upward) {
            delta = -delta;
        }
        result.visual.y = cursor.visual.y + delta;
    }
    return result;
}

edit_cursor_t tbuf_move_to_offset(const edit_tbuf_t *t, edit_cursor_t cursor, size_t offset) {
    if (offset == cursor.offset) {
        return cursor;
    }
    if (t->wrap_col <= 0) {
        size_t fwd = offset > cursor.offset ? offset - cursor.offset : 0;
        if (fwd > 1024) {
            for (;;) {
                edit_cursor_t next = tbuf_goto_line_start(t, cursor, cursor.logical.y + 1);
                if (next.offset > offset || next.offset <= cursor.offset) {
                    break;
                }
                cursor = next;
            }
        }
    }
    while (offset < cursor.offset) {
        cursor = tbuf_goto_line_start(t, cursor, cursor.logical.y - 1);
    }
    edit_doc_t doc;
    edit_measure_t m;
    tbuf_measure_cfg(t, &doc, &m);
    edit_measure_set_cursor(&m, cursor);
    return edit_measure_goto_offset(&m, offset);
}

edit_cursor_t tbuf_move_to_logical(const edit_tbuf_t *t, edit_cursor_t cursor, edit_point_t pos) {
    if (pos.x < 0) {
        pos.x = 0;
    }
    if (pos.y < 0) {
        pos.y = 0;
    }
    if (edit_point_eq(pos, cursor.logical)) {
        return cursor;
    }
    if (pos.y != cursor.logical.y || pos.x < cursor.logical.x) {
        cursor = tbuf_goto_line_start(t, cursor, pos.y);
    }
    edit_doc_t doc;
    edit_measure_t m;
    tbuf_measure_cfg(t, &doc, &m);
    edit_measure_set_cursor(&m, cursor);
    return edit_measure_goto_logical(&m, pos);
}

edit_cursor_t tbuf_move_to_visual(const edit_tbuf_t *t, edit_cursor_t cursor, edit_point_t pos) {
    if (pos.x < 0) {
        pos.x = 0;
    }
    if (pos.y < 0) {
        pos.y = 0;
    }
    if (edit_point_eq(pos, cursor.visual)) {
        return cursor;
    }
    if (t->wrap_col <= 0) {
        if (pos.y != cursor.logical.y || pos.x < cursor.logical.x) {
            cursor = tbuf_goto_line_start(t, cursor, pos.y);
        }
    } else {
        while (pos.y < cursor.visual.y) {
            cursor = tbuf_goto_line_start(t, cursor, cursor.logical.y - 1);
        }
        if (pos.y == cursor.visual.y && pos.x < cursor.visual.x) {
            cursor = tbuf_goto_line_start(t, cursor, cursor.logical.y);
        }
    }
    edit_doc_t doc;
    edit_measure_t m;
    tbuf_measure_cfg(t, &doc, &m);
    edit_measure_set_cursor(&m, cursor);
    return edit_measure_goto_visual(&m, pos);
}

static int32_t signum32(int32_t v) {
    return v > 0 ? 1 : v < 0 ? -1 : 0;
}

edit_cursor_t tbuf_move_delta(const edit_tbuf_t *t, edit_cursor_t cursor, edit_move_t granularity,
                              int32_t delta) {
    if (delta == 0) {
        return cursor;
    }
    int32_t sign = delta > 0 ? 1 : -1;
    if (granularity == EDIT_MOVE_GRAPHEME) {
        int32_t start_x = delta > 0 ? 0 : INT32_MAX;
        for (;;) {
            int32_t target_x = cursor.logical.x + delta;
            cursor = tbuf_move_to_logical(t, cursor, (edit_point_t){target_x, cursor.logical.y});
            delta = target_x - cursor.logical.x;
            if (signum32(delta) != sign || (delta < 0 && cursor.offset == 0) ||
                (delta > 0 && cursor.offset >= edit_gap_len(&t->buffer))) {
                break;
            }
            cursor =
                tbuf_move_to_logical(t, cursor, (edit_point_t){start_x, cursor.logical.y + sign});
            delta -= sign;
            if (signum32(delta) != sign || cursor.offset == 0 ||
                cursor.offset >= edit_gap_len(&t->buffer)) {
                break;
            }
        }
    } else {
        edit_doc_t doc;
        edit_gap_doc((edit_gap_t *)&t->buffer, &doc);
        size_t offset = t->cursor.offset;
        int32_t d = delta;
        while (d != 0) {
            if (d < 0) {
                offset = edit_word_backward(&doc, offset);
            } else {
                offset = edit_word_forward(&doc, offset);
            }
            d -= sign;
        }
        cursor = tbuf_move_to_offset(t, cursor, offset);
    }
    return cursor;
}

void edit_tbuf_goto_offset(edit_tbuf_t *t, size_t offset) {
    if (t == NULL) {
        return;
    }
    tbuf_set_cursor_internal(t, tbuf_move_to_offset(t, t->cursor, offset));
    t->hist_last = 0;
    edit_point_t z = {0, 0};
    tbuf_set_selection(t, false, z, z);
}

void edit_tbuf_goto_logical(edit_tbuf_t *t, edit_point_t pos) {
    if (t == NULL) {
        return;
    }
    tbuf_set_cursor_internal(t, tbuf_move_to_logical(t, t->cursor, pos));
    t->hist_last = 0;
    edit_point_t z = {0, 0};
    tbuf_set_selection(t, false, z, z);
}

void edit_tbuf_goto_visual(edit_tbuf_t *t, edit_point_t pos) {
    if (t == NULL) {
        return;
    }
    tbuf_set_cursor_internal(t, tbuf_move_to_visual(t, t->cursor, pos));
    t->hist_last = 0;
    edit_point_t z = {0, 0};
    tbuf_set_selection(t, false, z, z);
}

void edit_tbuf_move_delta(edit_tbuf_t *t, edit_move_t granularity, int32_t delta) {
    if (t == NULL) {
        return;
    }
    tbuf_set_cursor_internal(t, tbuf_move_delta(t, t->cursor, granularity, delta));
    t->hist_last = 0;
    edit_point_t z = {0, 0};
    tbuf_set_selection(t, false, z, z);
}

edit_point_t edit_tbuf_cursor_logical(const edit_tbuf_t *t) {
    edit_point_t z = {0, 0};
    return t == NULL ? z : t->cursor.logical;
}

edit_point_t edit_tbuf_cursor_visual(const edit_tbuf_t *t) {
    edit_point_t z = {0, 0};
    return t == NULL ? z : t->cursor.visual;
}

size_t edit_tbuf_cursor_offset(const edit_tbuf_t *t) {
    return t == NULL ? 0 : t->cursor.offset;
}

int32_t edit_tbuf_margin_width(const edit_tbuf_t *t) {
    return t == NULL ? 0 : t->margin_width;
}

int32_t edit_tbuf_text_width(const edit_tbuf_t *t) {
    return t == NULL ? 0 : t->width - t->margin_width;
}

void edit_tbuf_make_visible(edit_tbuf_t *t) {
    if (t != NULL) {
        t->wants_visibility = true;
    }
}

bool edit_tbuf_take_visibility(edit_tbuf_t *t) {
    if (t == NULL || !t->wants_visibility) {
        return false;
    }
    t->wants_visibility = false;
    return true;
}

void tbuf_reflow(edit_tbuf_t *t, bool force);

void edit_tbuf_set_margin(edit_tbuf_t *t, bool enabled) {
    if (t == NULL || t->margin_enabled == enabled) {
        return;
    }
    t->margin_enabled = enabled;
    tbuf_reflow(t, true);
}

bool edit_tbuf_set_width(edit_tbuf_t *t, int32_t width) {
    if (t == NULL || width <= 0 || width == t->width) {
        return false;
    }
    t->width = width;
    tbuf_reflow(t, true);
    return true;
}

int32_t edit_tbuf_tab_size(const edit_tbuf_t *t) {
    return t == NULL ? 4 : t->tab_size;
}

bool edit_tbuf_set_tab_size(edit_tbuf_t *t, int32_t width) {
    if (t == NULL) {
        return false;
    }
    if (width < 1) {
        width = 1;
    }
    if (width > 8) {
        width = 8;
    }
    if (width == t->tab_size) {
        return false;
    }
    t->tab_size = width;
    tbuf_reflow(t, true);
    return true;
}

bool edit_tbuf_indent_with_tabs(const edit_tbuf_t *t) {
    return t != NULL && t->indent_tabs;
}

void edit_tbuf_set_indent_tabs(edit_tbuf_t *t, bool enabled) {
    if (t != NULL) {
        t->indent_tabs = enabled;
    }
}

void edit_tbuf_set_wrap(edit_tbuf_t *t, bool enabled) {
    if (t == NULL || t->wrap_enabled == enabled) {
        return;
    }
    t->wrap_enabled = enabled;
    t->width = 0;
    edit_tbuf_make_visible(t);
}

void tbuf_reflow(edit_tbuf_t *t, bool force) {
    t->margin_width =
        t->margin_enabled ? digits10(t->logical_lines < 1 ? 1 : t->logical_lines) + 3 : 0;
    int32_t text_width = t->width - t->margin_width;
    int32_t wrap_col = (t->wrap_enabled && text_width >= 2) ? text_width : 0;

    if (force || t->wrap_col > wrap_col) {
        t->wrap_col = wrap_col;
        if (t->cursor.offset != 0) {
            edit_cursor_t origin;
            memset(&origin, 0, sizeof origin);
            t->cursor = tbuf_move_to_logical(t, origin, t->cursor.logical);
        }
        if (t->wrap_enabled) {
            edit_point_t max = {INT32_MAX, INT32_MAX};
            edit_cursor_t end = tbuf_move_to_logical(t, t->cursor, max);
            t->visual_lines = end.visual.y + 1;
        } else {
            t->visual_lines = t->logical_lines;
        }
    }
    t->has_render_cursor = false;
}

void edit_tbuf_read_fwd(const edit_tbuf_t *t, size_t off, const uint8_t **p, size_t *n) {
    if (t == NULL) {
        if (p != NULL) {
            *p = NULL;
        }
        if (n != NULL) {
            *n = 0;
        }
        return;
    }
    edit_gap_read_fwd(&t->buffer, off, p, n);
}

void edit_tbuf_read_bwd(const edit_tbuf_t *t, size_t off, const uint8_t **p, size_t *n) {
    if (t == NULL) {
        if (p != NULL) {
            *p = NULL;
        }
        if (n != NULL) {
            *n = 0;
        }
        return;
    }
    edit_gap_read_bwd(&t->buffer, off, p, n);
}
