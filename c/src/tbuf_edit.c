#include "edit/tbuf.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "edit/nav.h"
#include "edit/simd.h"

#include "tbuf_priv.h"

void edit_tbuf_destroy(edit_tbuf_t *t) {
    if (t == NULL) {
        return;
    }
    tbuf_search_free(t);
    for (size_t i = 0; i < 2; ++i) {
        edit_hist_t *stack = i == 0 ? t->undo : t->redo;
        size_t len = i == 0 ? t->undo_len : t->redo_len;
        for (size_t k = 0; k < len; ++k) {
            free(stack[k].deleted);
            free(stack[k].added);
        }
        free(stack);
    }
    edit_gap_destroy(&t->buffer);
    memset(t, 0, sizeof *t);
}

// Appends to a heap byte buffer; false on OOM.
static bool buf_append(uint8_t **ptr, size_t *len, size_t *cap, const uint8_t *src, size_t n) {
    if (n == 0) {
        return true;
    }
    if (src == NULL) {
        return false;
    }
    if (*len > (size_t)-1 - n) {
        return false;
    }
    size_t need = *len + n;
    if (need > *cap) {
        size_t grown = *cap != 0 ? *cap : 64;
        while (grown < need) {
            if (grown > (size_t)-1 / 2) {
                grown = need;
                break;
            }
            grown *= 2;
        }
        uint8_t *nb = (uint8_t *)realloc(*ptr, grown);
        if (nb == NULL) {
            return false;
        }
        *ptr = nb;
        *cap = grown;
    }
    memcpy(*ptr + *len, src, n);
    *len = need;
    return true;
}

static void hist_free(edit_hist_t *h) {
    free(h->deleted);
    free(h->added);
    memset(h, 0, sizeof *h);
}

// Pushes a history entry, capping the stack at 1000 (drops oldest).
static bool hist_push(edit_hist_t **stack, size_t *len, size_t *cap, const edit_hist_t *e) {
    while (*len > 1000) {
        hist_free(&(*stack)[0]);
        memmove(*stack, *stack + 1, (*len - 1) * sizeof **stack);
        *len -= 1;
    }
    if (*len == *cap) {
        size_t grown = *cap != 0 ? *cap * 2 : 8;
        edit_hist_t *nb = (edit_hist_t *)realloc(*stack, grown * sizeof **stack);
        if (nb == NULL) {
            return false;
        }
        *stack = nb;
        *cap = grown;
    }
    (*stack)[*len] = *e;
    *len += 1;
    return true;
}

static void hist_clear(edit_hist_t **stack, size_t *len) {
    for (size_t i = 0; i < *len; ++i) {
        hist_free(&(*stack)[i]);
    }
    *len = 0;
}

// Moves the last entry from one stack to the other (undo/redo transfer).
static bool hist_transfer(edit_hist_t **from, size_t *from_len, edit_hist_t **to, size_t *to_len,
                          size_t *to_cap) {
    if (*from_len == 0) {
        return false;
    }
    edit_hist_t e = (*from)[*from_len - 1];
    *from_len -= 1;
    if (!hist_push(to, to_len, to_cap, &e)) {
        (*from)[*from_len] = e;
        *from_len += 1;
        return false;
    }
    return true;
}

static void tbuf_undo_redo(edit_tbuf_t *t, bool undo);

void tbuf_edit_begin(edit_tbuf_t *t, int hist_type, edit_cursor_t cursor) {
    t->edit_depth += 1;
    if (t->edit_depth > 1) {
        return;
    }
    edit_cursor_t cursor_before = t->cursor;
    tbuf_set_cursor_internal(t, cursor);

    if (hist_type != t->hist_last || (hist_type != 1 && hist_type != 2)) {
        hist_clear(&t->redo, &t->redo_len);
        edit_hist_t e;
        memset(&e, 0, sizeof e);
        e.cursor_before = cursor_before.logical;
        e.has_selection = t->has_selection;
        e.sel_beg = t->sel_beg;
        e.sel_end = t->sel_end;
        e.logical_before = t->logical_lines;
        e.visual_before = t->visual_lines;
        e.generation_before = edit_gap_generation(&t->buffer);
        e.cursor = cursor.logical;
        t->hist_last = hist_type;
        hist_push(&t->undo, &t->undo_len, &t->undo_cap, &e);
    }
    t->edit_off = cursor.offset;

    if (t->wrap_col > 0) {
        edit_cursor_t safe_start = tbuf_goto_line_start(t, cursor, cursor.logical.y);
        edit_point_t next = {0, cursor.logical.y + 1};
        edit_cursor_t next_line = tbuf_move_to_logical(t, cursor, next);
        t->has_edit_info = true;
        t->edit_safe_start = safe_start;
        t->edit_line_height = next_line.visual.y - safe_start.visual.y;
        t->edit_next_dist = next_line.offset > cursor.offset ? next_line.offset - cursor.offset : 0;
    }
}

static edit_hist_t *edit_entry(edit_tbuf_t *t) {
    assert(t->undo_len > 0);
    if (t->undo_len == 0) {
        return NULL;
    }
    return &t->undo[t->undo_len - 1];
}

static void edit_write(edit_tbuf_t *t, const uint8_t *text, size_t len) {
    int32_t y_before = t->cursor.logical.y;
    edit_hist_t *undo = edit_entry(t);
    if (undo == NULL) {
        return;
    }
    if (!buf_append(&undo->added, &undo->added_len, &undo->added_cap, text, len)) {
        return;
    }
    edit_gap_replace(&t->buffer, t->edit_off, t->edit_off, text, len);
    t->edit_off += len;
    t->cursor = tbuf_move_to_offset(t, t->cursor, t->edit_off);
    t->logical_lines += t->cursor.logical.y - y_before;
}

void tbuf_edit_delete(edit_tbuf_t *t, edit_cursor_t to) {
    assert(to.offset >= t->edit_off);
    if (to.offset < t->edit_off) {
        return;
    }
    int32_t y_before = t->cursor.logical.y;
    size_t off = t->edit_off;
    edit_hist_t *undo = edit_entry(t);
    if (undo == NULL) {
        return;
    }
    if (edit_point_cmp(t->cursor.logical, undo->cursor) < 0) {
        // Prepend the deleted portion and note its start.
        size_t old = undo->deleted_len;
        size_t count = to.offset - off;
        if (count > 0) {
            if (old > (size_t)-1 - count) {
                return;
            }
            size_t need = old + count;
            if (need > undo->deleted_cap) {
                size_t grown = undo->deleted_cap != 0 ? undo->deleted_cap : 64;
                while (grown < need) {
                    if (grown > (size_t)-1 / 2) {
                        grown = need;
                        break;
                    }
                    grown *= 2;
                }
                uint8_t *tmp = (uint8_t *)realloc(undo->deleted, grown);
                if (tmp == NULL) {
                    return;
                }
                undo->deleted = tmp;
                undo->deleted_cap = grown;
            }
            memmove(undo->deleted + count, undo->deleted, old);
            // Copy the deleted portion in chunks.
            size_t got = 0;
            while (got < count) {
                uint8_t tmp[4096];
                size_t want = count - got;
                if (want > sizeof tmp) {
                    want = sizeof tmp;
                }
                size_t k = edit_gap_extract(&t->buffer, off + got, off + got + want, tmp, want);
                if (k == 0) {
                    break;
                }
                memcpy(undo->deleted + got, tmp, k);
                got += k;
            }
            undo->deleted_len = old + got;
            undo->cursor = t->cursor.logical;
        }
    } else {
        size_t got = 0;
        size_t count = to.offset - off;
        while (got < count) {
            uint8_t tmp[4096];
            size_t want = count - got;
            if (want > sizeof tmp) {
                want = sizeof tmp;
            }
            size_t k = edit_gap_extract(&t->buffer, off + got, off + got + want, tmp, want);
            if (k == 0 ||
                !buf_append(&undo->deleted, &undo->deleted_len, &undo->deleted_cap, tmp, k)) {
                break;
            }
            got += k;
        }
    }
    size_t gap = 0;
    edit_gap_allocate(&t->buffer, off, 0, to.offset - off, &gap);
    (void)gap;
    t->logical_lines += y_before - to.logical.y;
}

void tbuf_edit_end(edit_tbuf_t *t) {
    assert(t->edit_depth > 0);
    t->edit_depth -= 1;
    if (t->edit_depth > 0) {
        return;
    }
#ifndef NDEBUG
    {
        edit_hist_t *e = edit_entry(t);
        assert(e == NULL || e->deleted_len > 0 || e->added_len > 0);
    }
#endif
    if (t->has_edit_info) {
        t->has_edit_info = false;
        edit_hist_t *e = edit_entry(t);
        size_t deleted_count = e == NULL ? 0 : e->deleted_len;
        edit_point_t target = t->cursor.logical;
        edit_doc_t doc;
        edit_measure_t m;
        tbuf_measure_cfg(t, &doc, &m);
        // Re-measure the cursor from the safe line start.
        edit_cursor_t safe = t->edit_safe_start;
        (void)safe;
        tbuf_set_cursor_internal(t, tbuf_move_to_logical(t, t->edit_safe_start, target));
        if (deleted_count < t->edit_next_dist) {
            edit_point_t nl = {0, target.y + 1};
            edit_cursor_t next_line = tbuf_move_to_logical(t, t->cursor, nl);
            int32_t before = t->edit_line_height;
            int32_t after = next_line.visual.y - t->edit_safe_start.visual.y;
            t->visual_lines += after - before;
        } else {
            edit_point_t max = {INT32_MAX, INT32_MAX};
            edit_cursor_t end = tbuf_move_to_logical(t, t->cursor, max);
            t->visual_lines = end.visual.y + 1;
        }
    } else {
        t->visual_lines = t->logical_lines;
    }
    tbuf_update_margin(t);
    tbuf_search_free(t);
    t->has_render_cursor = false;
}

void edit_tbuf_undo(edit_tbuf_t *t) {
    if (t != NULL) {
        tbuf_undo_redo(t, true);
    }
}

void edit_tbuf_redo(edit_tbuf_t *t) {
    if (t != NULL) {
        tbuf_undo_redo(t, false);
    }
}

static void tbuf_undo_redo(edit_tbuf_t *t, bool undo) {
    if (undo) {
        if (!hist_transfer(&t->undo, &t->undo_len, &t->redo, &t->redo_len, &t->redo_cap)) {
            return;
        }
    } else {
        if (!hist_transfer(&t->redo, &t->redo_len, &t->undo, &t->undo_len, &t->undo_cap)) {
            return;
        }
    }
    edit_hist_t *change = undo ? &t->redo[t->redo_len - 1] : &t->undo[t->undo_len - 1];

    edit_cursor_t cursor = tbuf_move_to_logical(t, t->cursor, change->cursor);
    edit_cursor_t safe = cursor;
    if (t->wrap_col > 0) {
        safe = tbuf_goto_line_start(t, cursor, cursor.logical.y);
    }

    uint32_t buffer_gen = edit_gap_generation(&t->buffer);
    // Undo: deleted becomes added and vice versa.
    uint8_t *tmp_p = change->deleted;
    change->deleted = change->added;
    change->added = tmp_p;
    size_t tmp_n = change->deleted_len;
    change->deleted_len = change->added_len;
    change->added_len = tmp_n;
    tmp_n = change->deleted_cap;
    change->deleted_cap = change->added_cap;
    change->added_cap = tmp_n;

    size_t gap = 0;
    edit_gap_allocate(&t->buffer, cursor.offset, 0, change->deleted_len, &gap);
    (void)gap;

    // Reinsert line by line with CRLF translation.
    {
        size_t beg = 0;
        size_t offset = cursor.offset;
        while (beg < change->added_len) {
            size_t end = beg;
            int32_t line = 0;
            edit_newlines_forward(change->added, change->added_len, beg, 0, 1, &end, &line);
            bool has_nl = line != 0;
            size_t link_len = edit_strip_newline(change->added + beg, end - beg);
            size_t wgap = 0;
            uint8_t *dst = edit_gap_allocate(&t->buffer, offset, link_len + 2, 0, &wgap);
            size_t written = 0;
            if (dst != NULL) {
                size_t k = link_len < wgap ? link_len : wgap;
                memcpy(dst, change->added + beg, k);
                written = k;
                if (has_nl) {
                    if (t->newlines_crlf && written < wgap) {
                        dst[written++] = '\r';
                    }
                    if (written < wgap) {
                        dst[written++] = '\n';
                    }
                }
            }
            edit_gap_commit(&t->buffer, written);
            beg = end;
            offset += written;
        }
    }

    int32_t tmp_lines = t->logical_lines;
    t->logical_lines = change->logical_before;
    change->logical_before = tmp_lines;
    tmp_lines = t->visual_lines;
    t->visual_lines = change->visual_before;
    change->visual_before = tmp_lines;

    bool tmp_sel = t->has_selection;
    edit_point_t tmp_beg = t->sel_beg;
    edit_point_t tmp_end = t->sel_end;
    t->has_selection = change->has_selection;
    t->sel_beg = change->sel_beg;
    t->sel_end = change->sel_end;
    change->has_selection = tmp_sel;
    change->sel_beg = tmp_beg;
    change->sel_end = tmp_end;

    edit_gap_set_generation(&t->buffer, change->generation_before);
    change->generation_before = buffer_gen;

    edit_cursor_t cursor_before = tbuf_move_to_logical(t, safe, change->cursor_before);
    change->cursor_before = t->cursor.logical;
    t->cursor = cursor_before;

    if (t->undo_len == 0) {
        t->hist_last = 0;
    }
    tbuf_reflow(t, false);
}

void edit_tbuf_write(edit_tbuf_t *t, const uint8_t *text, size_t len, bool raw) {
    if (t == NULL || (len > 0 && text == NULL)) {
        return;
    }
    if (len == 0) {
        return;
    }
    edit_cursor_t beg;
    edit_cursor_t end;
    if (edit_tbuf_selection_range(t, &beg, &end)) {
        tbuf_edit_begin(t, 1, beg);
        tbuf_edit_delete(t, end);
        edit_point_t z = {0, 0};
        tbuf_set_selection(t, false, z, z);
    }
    if (t->edit_depth <= 0) {
        tbuf_edit_begin(t, 1, t->cursor);
    }

    static const char spaces[] = "                    ";
    size_t offset = 0;
    while (offset < len) {
        // Lines split on CR or LF (bracketed paste may use bare CR).
        size_t next = edit_memchr2('\r', '\n', text, len, offset);
        size_t line_len = next - offset;
        int32_t column_before = t->cursor.logical.x;

        size_t line_off = 0;
        while (line_off < line_len) {
            size_t plain_len = line_len - line_off;
            if (!raw && !t->indent_tabs) {
                size_t tab = line_off;
                while (tab < line_len && text[offset + tab] != '\t') {
                    ++tab;
                }
                plain_len = tab - line_off;
            }
            if (plain_len > 0) {
                edit_write(t, text + offset + line_off, plain_len);
                line_off += plain_len;
            } else if (raw || t->indent_tabs) {
                break;
            }
            while (line_off < line_len && text[offset + line_off] == '\t') {
                int32_t spaces_n = t->tab_size - (t->cursor.column % t->tab_size);
                if (spaces_n < 0) {
                    spaces_n = 0;
                }
                if (spaces_n > (int32_t)sizeof spaces - 1) {
                    spaces_n = (int32_t)sizeof spaces - 1;
                }
                edit_write(t, (const uint8_t *)spaces, (size_t)spaces_n);
                line_off += 1;
            }
        }

        if (!raw && t->overtype) {
            int32_t delete_n = t->cursor.logical.x - column_before;
            edit_point_t target = {t->cursor.logical.x + delete_n, t->cursor.logical.y};
            edit_cursor_t endc = tbuf_move_to_logical(t, t->cursor, target);
            tbuf_edit_delete(t, endc);
        }

        offset += line_len;
        if (offset >= len) {
            break;
        }

        // Newline with previous-line indentation.
        uint8_t nlbuf[128];
        size_t nllen = 0;
        if (t->newlines_crlf) {
            nlbuf[nllen++] = '\r';
        }
        nlbuf[nllen++] = '\n';
        if (!raw) {
            // Measure the current line's indentation.
            edit_cursor_t line_beg = tbuf_goto_line_start(t, t->cursor, t->cursor.logical.y);
            size_t limit = t->cursor.offset;
            size_t off = line_beg.offset;
            size_t indentation = 0;
            while (off < limit) {
                const uint8_t *chunk = NULL;
                size_t chunk_len = 0;
                edit_gap_read_fwd(&t->buffer, off, &chunk, &chunk_len);
                if (chunk == NULL || chunk_len == 0) {
                    break;
                }
                if (chunk_len > limit - off) {
                    chunk_len = limit - off;
                }
                bool stop = false;
                for (size_t i = 0; i < chunk_len; ++i) {
                    if (chunk[i] == ' ') {
                        indentation += 1;
                    } else if (chunk[i] == '\t') {
                        size_t ts = t->tab_size > 0 ? (size_t)t->tab_size : 1;
                        indentation += ts - (indentation % ts);
                    } else {
                        stop = true;
                        break;
                    }
                }
                if (stop) {
                    break;
                }
                off += chunk_len;
            }
            if (t->indent_tabs) {
                size_t ts = t->tab_size > 0 ? (size_t)t->tab_size : 1;
                size_t tabs = indentation / ts;
                indentation -= tabs * ts;
                while (tabs > 0 && nllen < sizeof nlbuf) {
                    nlbuf[nllen++] = '\t';
                    tabs -= 1;
                }
            }
            while (indentation > 0 && nllen < sizeof nlbuf) {
                nlbuf[nllen++] = ' ';
                indentation -= 1;
            }
        }
        edit_write(t, nlbuf, nllen);

        // Skip one CR/LF/CRLF.
        if (offset < len && text[offset] == '\r') {
            offset += 1;
        }
        if (offset < len && text[offset] == '\n') {
            offset += 1;
        }
    }

    tbuf_edit_end(t);
}

void edit_tbuf_delete(edit_tbuf_t *t, edit_move_t granularity, int32_t delta) {
    if (t == NULL) {
        return;
    }
    assert(delta == -1 || delta == 1);
    if (delta != -1 && delta != 1) {
        return;
    }
    edit_cursor_t beg;
    edit_cursor_t end;
    if (edit_tbuf_selection_range(t, &beg, &end)) {
        // Use the selection; ignore delta.
    } else {
        if ((delta == -1 && t->cursor.offset == 0) ||
            (delta == 1 && t->cursor.offset >= edit_gap_len(&t->buffer))) {
            return;
        }
        beg = t->cursor;
        end = tbuf_move_delta(t, t->cursor, granularity, delta);
        if (beg.offset == end.offset) {
            return;
        }
        if (beg.offset > end.offset) {
            edit_cursor_t tmp = beg;
            beg = end;
            end = tmp;
        }
    }
    tbuf_edit_begin(t, 2, beg);
    tbuf_edit_delete(t, end);
    tbuf_edit_end(t);
    edit_point_t z = {0, 0};
    tbuf_set_selection(t, false, z, z);
}

void edit_tbuf_copy_from(edit_tbuf_t *t, const edit_doc_t *src) {
    if (t == NULL || src == NULL) {
        return;
    }
    if (!edit_gap_copy_from(&t->buffer, src)) {
        return;
    }
    hist_clear(&t->undo, &t->undo_len);
    hist_clear(&t->redo, &t->redo_len);
    t->hist_last = 0;
    memset(&t->cursor, 0, sizeof t->cursor);
    t->has_render_cursor = false;
    edit_point_t z = {0, 0};
    tbuf_set_selection(t, false, z, z);
    t->save_generation = edit_gap_generation(&t->buffer);
    tbuf_reflow(t, true);
    // Keep only the first line (single-line edit fields).
    edit_point_t max0 = {INT32_MAX, 0};
    t->cursor = tbuf_move_to_logical(t, t->cursor, max0);
    size_t delete = edit_gap_len(&t->buffer) - t->cursor.offset;
    if (delete != 0) {
        size_t gap = 0;
        edit_gap_allocate(&t->buffer, t->cursor.offset, 0, delete, &gap);
        (void)gap;
    }
}

void edit_tbuf_save_to(edit_tbuf_t *t, edit_doc_t *dst) {
    if (t == NULL || dst == NULL) {
        return;
    }
    edit_gap_copy_into(&t->buffer, dst);
    t->save_generation = edit_gap_generation(&t->buffer);
}

void edit_tbuf_normalize_newlines(edit_tbuf_t *t, bool crlf) {
    if (t == NULL) {
        return;
    }
    const uint8_t *newline = crlf ? (const uint8_t *)"\r\n" : (const uint8_t *)"\n";
    size_t newline_len = crlf ? 2 : 1;
    size_t off = 0;
    size_t cursor_offset = t->cursor.offset;
    size_t render_offset = t->has_render_cursor ? t->render_cursor.offset : cursor_offset;
    for (;;) {
        // Seek to the next line start.
        for (;;) {
            const uint8_t *chunk = NULL;
            size_t chunk_len = 0;
            edit_gap_read_fwd(&t->buffer, off, &chunk, &chunk_len);
            if (chunk == NULL || chunk_len == 0) {
                goto done;
            }
            size_t delta = 0;
            int32_t line = 0;
            edit_newlines_forward(chunk, chunk_len, 0, 0, 1, &delta, &line);
            off += delta;
            if (line == 1) {
                break;
            }
        }
        const uint8_t *chunk = NULL;
        size_t chunk_len = 0;
        edit_gap_read_bwd(&t->buffer, off, &chunk, &chunk_len);
        if (chunk == NULL || chunk_len == 0) {
            goto done;
        }
        size_t nl_len =
            (chunk_len >= 2 && chunk[chunk_len - 2] == '\r' && chunk[chunk_len - 1] == '\n') ? 2
                                                                                             : 1;
        if (nl_len != newline_len || memcmp(chunk + chunk_len - nl_len, newline, nl_len) != 0) {
            long delta = (long)newline_len - (long)nl_len;
            if (off <= cursor_offset) {
                long updated = (long)cursor_offset + delta;
                cursor_offset = updated < 0 ? 0 : (size_t)updated;
            }
            if (off <= render_offset) {
                long updated = (long)render_offset + delta;
                render_offset = updated < 0 ? 0 : (size_t)updated;
            }
            off -= nl_len;
            edit_gap_replace(&t->buffer, off, off + nl_len, newline, newline_len);
            off += newline_len;
        }
    }
done:
    t->cursor.offset = cursor_offset;
    if (t->has_render_cursor) {
        t->render_cursor.offset = render_offset;
    }
    t->newlines_crlf = crlf;
}

edit_point_t edit_tbuf_indent_end(edit_tbuf_t *t) {
    edit_point_t none = {0, 0};
    if (t == NULL) {
        return none;
    }
    edit_cursor_t line = tbuf_goto_line_start(t, t->cursor, t->cursor.logical.y);
    int32_t chars = 0;
    size_t offset = line.offset;
    for (;;) {
        const uint8_t *chunk = NULL;
        size_t chunk_len = 0;
        edit_gap_read_fwd(&t->buffer, offset, &chunk, &chunk_len);
        if (chunk == NULL || chunk_len == 0) {
            break;
        }
        size_t i = 0;
        while (i < chunk_len) {
            uint8_t c = chunk[i];
            if (c == '\n' || c == '\r' || (c != ' ' && c != '\t')) {
                break;
            }
            chars += 1;
            i += 1;
        }
        if (i < chunk_len) {
            break;
        }
        offset += chunk_len;
    }
    edit_point_t out = {chars, line.logical.y};
    return out;
}

void edit_tbuf_unindent(edit_tbuf_t *t) {
    if (t == NULL) {
        return;
    }
    edit_point_t sel_beg = t->cursor.logical;
    edit_point_t sel_end = sel_beg;
    if (t->has_selection) {
        sel_beg = t->sel_beg;
        sel_end = t->sel_end;
    }
    edit_point_t lo = sel_beg;
    edit_point_t hi = sel_end;
    if (edit_point_cmp(lo, hi) > 0) {
        edit_point_t tmp = lo;
        lo = hi;
        hi = tmp;
    }
    edit_cursor_t beg = tbuf_move_to_logical(t, t->cursor, (edit_point_t){0, lo.y});
    edit_cursor_t end = tbuf_move_to_logical(t, beg, (edit_point_t){INT32_MAX, hi.y});

    size_t span = end.offset > beg.offset ? end.offset - beg.offset : 0;
    uint8_t *replacement = NULL;
    if (span > 0) {
        replacement = (uint8_t *)malloc(span);
        if (replacement == NULL) {
            return;
        }
        size_t got = edit_gap_extract(&t->buffer, beg.offset, end.offset, replacement, span);
        if (got != span) {
            free(replacement);
            return;
        }
    }
    size_t initial_len = span;
    size_t replen = span;
    size_t offset = 0;
    int32_t y = beg.logical.y;
    int32_t tab_size = t->tab_size > 0 ? t->tab_size : 1;
    while (offset < replen) {
        size_t remove = 0;
        if (replacement[offset] == '\t') {
            remove = 1;
        } else {
            while (remove < (size_t)tab_size && offset + remove < replen &&
                   replacement[offset + remove] == ' ') {
                remove += 1;
            }
        }
        if (remove > 0) {
            memmove(replacement + offset, replacement + offset + remove, replen - offset - remove);
            replen -= remove;
        }
        if (y == sel_beg.y) {
            sel_beg.x -= (int32_t)remove;
        }
        if (y == sel_end.y) {
            sel_end.x -= (int32_t)remove;
        }
        size_t next = offset;
        int32_t line = y;
        edit_newlines_forward(replacement, replen, offset, y, y + 1, &next, &line);
        offset = next;
        y = line;
        if (offset >= replen) {
            break;
        }
    }

    if (replen == initial_len) {
        free(replacement);
        return;
    }
    tbuf_edit_begin(t, 0, beg);
    tbuf_edit_delete(t, end);
    if (replen > 0) {
        edit_write(t, replacement, replen);
    }
    free(replacement);
    tbuf_edit_end(t);

    if (t->has_selection) {
        t->sel_beg = sel_beg;
        t->sel_end = sel_end;
    }
    t->cursor = tbuf_move_to_logical(t, t->cursor, sel_end);
}
