#include "edit/measure.h"

#include <string.h>

#include "edit/simd.h"
#include "edit/ucd.h"
#include "edit/utf8.h"

void edit_measure_init(edit_measure_t *m, const edit_doc_t *doc) {
    if (m == NULL) {
        return;
    }
    m->doc = doc;
    m->tab_size = 8;
    m->wrap_col = 0;
    memset(&m->cursor, 0, sizeof m->cursor);
}

void edit_measure_set_tab(edit_measure_t *m, edit_coord_t tab_size) {
    if (m == NULL) {
        return;
    }
    m->tab_size = tab_size >= 1 ? tab_size : 1;
}

void edit_measure_set_wrap(edit_measure_t *m, edit_coord_t wrap_col) {
    if (m != NULL) {
        m->wrap_col = wrap_col;
    }
}

void edit_measure_set_cursor(edit_measure_t *m, edit_cursor_t cursor) {
    if (m != NULL) {
        m->cursor = cursor;
    }
}

edit_cursor_t edit_measure_cursor(const edit_measure_t *m) {
    edit_cursor_t c;
    memset(&c, 0, sizeof c);
    if (m != NULL) {
        c = m->cursor;
    }
    return c;
}

static edit_coord_t calc_target_x(edit_point_t target, edit_coord_t pos_y) {
    if (pos_y < target.y) {
        return INT32_MAX;
    }
    if (pos_y == target.y) {
        return target.x;
    }
    return 0;
}

// Grapheme iterator state over doc chunks.
typedef struct {
    const edit_doc_t *doc;
    edit_utf8_chars_t chars;
    size_t range_beg;
    size_t range_end;
} graphemes_t;

static void graphemes_init(graphemes_t *g, const edit_doc_t *doc, size_t start) {
    g->doc = doc;
    edit_utf8_chars_init(&g->chars, NULL, 0, 0);
    g->range_beg = start;
    g->range_end = start;
}

// Loads the chunk starting at range_end. Call when !has_next().
static void graphemes_refill(graphemes_t *g) {
    const uint8_t *ptr = NULL;
    size_t len = 0;
    if (g->doc != NULL && g->doc->read_fwd != NULL) {
        g->doc->read_fwd(g->doc->ctx, g->range_end, &ptr, &len);
    }
    if (ptr == NULL) {
        len = 0;
    }
    edit_utf8_chars_init(&g->chars, ptr, len, 0);
    g->range_beg = g->range_end;
    g->range_end += len;
}

static edit_cursor_t measure_forward(edit_coord_t tab_size, edit_coord_t wrap_col,
                                     size_t offset_target, edit_point_t logical_target,
                                     edit_point_t visual_target, edit_cursor_t cursor,
                                     const edit_doc_t *doc) {
    if (cursor.offset >= offset_target || edit_point_cmp(cursor.logical, logical_target) >= 0 ||
        edit_point_cmp(cursor.visual, visual_target) >= 0) {
        return cursor;
    }

    size_t offset = cursor.offset;
    edit_coord_t lx = cursor.logical.x;
    edit_coord_t ly = cursor.logical.y;
    edit_coord_t vx = cursor.visual.x;
    edit_coord_t vy = cursor.visual.y;
    edit_coord_t column = cursor.column;

    edit_coord_t ltx = calc_target_x(logical_target, ly);
    edit_coord_t vtx = calc_target_x(visual_target, vy);

    bool wrap_opp = cursor.wrap_opp;
    size_t wrap_off = offset;
    edit_coord_t wrap_lx = lx;
    edit_coord_t wrap_vx = vx;
    edit_coord_t wrap_col_state = column;

    graphemes_t it;
    graphemes_init(&it, doc, offset);
    size_t chunk_start = offset; // == range_end after refill; tracks cluster base
    size_t next_props = edit_ucd_start_props();

    for (;;) {
        if (offset >= offset_target || lx >= ltx || vx >= vtx) {
            break;
        }

        size_t cur_props = next_props;
        size_t last_char = next_props;
        size_t next_off = 0;
        uint32_t state = 0;
        edit_coord_t width = 0;

        // Seek to the next cluster, accumulating its width.
        for (;;) {
            if (!edit_utf8_has_next(&it.chars)) {
                graphemes_refill(&it);
                chunk_start = it.range_beg;
            }
            last_char = next_props;
            next_off = chunk_start + edit_utf8_offset(&it.chars);
            width += (edit_coord_t)edit_ucd_width(next_props);

            uint32_t ch = 0;
            if (!edit_utf8_next(&it.chars, &ch)) {
                break;
            }
            next_props = edit_ucd_lookup(ch);
            state = edit_ucd_joins(state, last_char, next_props);
            if (edit_ucd_joins_done(state)) {
                break;
            }
        }

        if (next_off == offset) {
            if (it.chars.len == 0) {
                break; // end of text
            }
            continue; // first iteration at start-of-text
        }

        if (width > 2) {
            width = 2;
        }
        if (last_char == edit_ucd_tab_props()) {
            width = tab_size - (column % tab_size);
        }

        if (last_char == edit_ucd_lf_props()) {
            wrap_opp = false;
            if (ly >= logical_target.y || vy >= visual_target.y) {
                break;
            }
            offset = next_off;
            lx = 0;
            ly += 1;
            vx = 0;
            vy += 1;
            column = 0;
            ltx = calc_target_x(logical_target, ly);
            vtx = calc_target_x(visual_target, vy);
            continue;
        }

        if ((int64_t)vx + width > (int64_t)vtx) {
            break;
        }

        if (wrap_col > 0 && (int64_t)vx + width > (int64_t)wrap_col) {
            if (!wrap_opp) {
                wrap_off = offset;
                wrap_lx = lx;
                wrap_vx = vx;
                wrap_col_state = column;
                vx = 0;
            } else {
                vx -= wrap_vx;
            }
            wrap_opp = false;
            vy += 1;
            vtx = calc_target_x(visual_target, vy);
            if (vx == vtx) {
                break;
            }
            if ((int64_t)vx > (int64_t)vtx) {
                offset = wrap_off;
                lx = wrap_lx;
                vx = 0;
                column = wrap_col_state;
                // Rewind iteration to the wrap offset.
                graphemes_init(&it, doc, offset);
                next_props = edit_ucd_start_props();
                continue;
            }
        }

        offset = next_off;
        lx += 1;
        vx += width;
        column += width;

        if (wrap_col > 0 && !edit_ucd_line_joins(cur_props, next_props)) {
            wrap_opp = true;
            wrap_off = offset;
            wrap_lx = lx;
            wrap_vx = vx;
            wrap_col_state = column;
        }
    }

    if (wrap_col > 0) {
        if (wrap_opp && wrap_lx != lx && vy <= visual_target.y) {
            edit_coord_t look_vx = vx;
            for (;;) {
                size_t cur_props = next_props;
                size_t last_char = next_props;
                size_t next_off = 0;
                uint32_t state = 0;
                edit_coord_t width = 0;
                for (;;) {
                    if (!edit_utf8_has_next(&it.chars)) {
                        graphemes_refill(&it);
                        chunk_start = it.range_beg;
                    }
                    last_char = next_props;
                    next_off = chunk_start + edit_utf8_offset(&it.chars);
                    width += (edit_coord_t)edit_ucd_width(next_props);
                    uint32_t ch = 0;
                    if (!edit_utf8_next(&it.chars, &ch)) {
                        break;
                    }
                    next_props = edit_ucd_lookup(ch);
                    state = edit_ucd_joins(state, last_char, next_props);
                    if (edit_ucd_joins_done(state)) {
                        break;
                    }
                }
                if (next_off == offset) {
                    if (it.chars.len == 0) {
                        break;
                    }
                    continue;
                }
                if (width > 2) {
                    width = 2;
                }
                if (last_char == edit_ucd_tab_props()) {
                    width = tab_size - (column % tab_size);
                }
                if (last_char == edit_ucd_lf_props()) {
                    break;
                }
                look_vx += width;
                if (look_vx > wrap_col) {
                    vx -= wrap_vx;
                    vy += 1;
                    break;
                } else if (!edit_ucd_line_joins(cur_props, next_props)) {
                    break;
                }
            }
        }
        if (vy > visual_target.y) {
            offset = wrap_off;
            lx = wrap_lx;
            vx = wrap_vx;
            vy = visual_target.y;
            column = wrap_col_state;
            wrap_opp = true;
        }
    }

    edit_cursor_t out;
    out.offset = offset;
    out.logical.x = lx;
    out.logical.y = ly;
    out.visual.x = vx;
    out.visual.y = vy;
    out.column = column;
    out.wrap_opp = wrap_opp;
    return out;
}

edit_cursor_t edit_measure_goto_offset(edit_measure_t *m, size_t offset) {
    if (m == NULL) {
        edit_cursor_t c;
        memset(&c, 0, sizeof c);
        return c;
    }
    edit_point_t max = {INT32_MAX, INT32_MAX};
    m->cursor = measure_forward(m->tab_size, m->wrap_col, offset, max, max, m->cursor, m->doc);
    return m->cursor;
}

edit_cursor_t edit_measure_goto_logical(edit_measure_t *m, edit_point_t target) {
    if (m == NULL) {
        edit_cursor_t c;
        memset(&c, 0, sizeof c);
        return c;
    }
    edit_point_t max = {INT32_MAX, INT32_MAX};
    m->cursor =
        measure_forward(m->tab_size, m->wrap_col, (size_t)-1, target, max, m->cursor, m->doc);
    return m->cursor;
}

edit_cursor_t edit_measure_goto_visual(edit_measure_t *m, edit_point_t target) {
    if (m == NULL) {
        edit_cursor_t c;
        memset(&c, 0, sizeof c);
        return c;
    }
    edit_point_t max = {INT32_MAX, INT32_MAX};
    m->cursor =
        measure_forward(m->tab_size, m->wrap_col, (size_t)-1, max, target, m->cursor, m->doc);
    return m->cursor;
}

void edit_newlines_forward(const uint8_t *text, size_t len, size_t offset, edit_coord_t line,
                           edit_coord_t line_stop, size_t *out_off, edit_coord_t *out_line) {
    if (line >= line_stop) {
        edit_newlines_backward(text, len, offset, line, line_stop, out_off, out_line);
        return;
    }
    if (text == NULL) {
        len = 0;
    }
    if (offset > len) {
        offset = len;
    }
    for (;;) {
        offset = edit_memchr2('\n', '\n', text, len, offset);
        if (offset >= len) {
            break;
        }
        offset += 1;
        line += 1;
        if (line >= line_stop) {
            break;
        }
    }
    if (out_off != NULL) {
        *out_off = offset;
    }
    if (out_line != NULL) {
        *out_line = line;
    }
}

void edit_newlines_backward(const uint8_t *text, size_t len, size_t offset, edit_coord_t line,
                            edit_coord_t line_stop, size_t *out_off, edit_coord_t *out_line) {
    if (text == NULL) {
        len = 0;
    }
    if (offset > len) {
        offset = len;
    }
    for (;;) {
        size_t hit = 0;
        if (!edit_memrchr2('\n', '\n', text, len, offset, &hit)) {
            if (out_off != NULL) {
                *out_off = 0;
            }
            if (out_line != NULL) {
                *out_line = line;
            }
            return;
        }
        offset = hit;
        if (line <= line_stop) {
            if (out_off != NULL) {
                *out_off = offset + 1;
            }
            if (out_line != NULL) {
                *out_line = line;
            }
            return;
        }
        line -= 1;
    }
}

size_t edit_skip_newline(const uint8_t *text, size_t len, size_t offset) {
    if (text == NULL) {
        return 0;
    }
    if (offset >= len) {
        return offset;
    }
    if (text[offset] == '\r') {
        offset += 1;
    }
    if (offset >= len) {
        return offset;
    }
    if (text[offset] == '\n') {
        offset += 1;
    }
    return offset;
}

size_t edit_strip_newline(const uint8_t *text, size_t len) {
    if (text == NULL) {
        return 0;
    }
    if (len > 0 && text[len - 1] == '\n') {
        len -= 1;
    }
    if (len > 0 && text[len - 1] == '\r') {
        len -= 1;
    }
    return len;
}
