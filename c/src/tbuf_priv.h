// Shared TextBuffer internals between tbuf.c and tbuf_edit.c (not public API).
#ifndef EDIT_TBUF_PRIV_H
#define EDIT_TBUF_PRIV_H

#include "edit/doc.h"
#include "edit/helpers.h"
#include "edit/measure.h"
#include "edit/tbuf.h"

edit_cursor_t tbuf_move_to_offset(const edit_tbuf_t *t, edit_cursor_t cursor, size_t offset);
edit_cursor_t tbuf_move_to_logical(const edit_tbuf_t *t, edit_cursor_t cursor, edit_point_t pos);
edit_cursor_t tbuf_move_to_visual(const edit_tbuf_t *t, edit_cursor_t cursor, edit_point_t pos);
edit_cursor_t tbuf_move_delta(const edit_tbuf_t *t, edit_cursor_t cursor, edit_move_t granularity,
                              int32_t delta);
edit_cursor_t tbuf_goto_line_start(const edit_tbuf_t *t, edit_cursor_t cursor, int32_t y);
void tbuf_set_cursor_internal(edit_tbuf_t *t, edit_cursor_t cursor);
uint32_t tbuf_set_selection(edit_tbuf_t *t, bool has, edit_point_t beg, edit_point_t end);
void tbuf_reflow(edit_tbuf_t *t, bool force);
void tbuf_measure_cfg(const edit_tbuf_t *t, edit_doc_t *doc, edit_measure_t *m);
// Edit primitives (history-tracked); hist_type 1 = write, 2 = delete.
void tbuf_edit_begin(edit_tbuf_t *t, int hist_type, edit_cursor_t cursor);
void tbuf_edit_delete(edit_tbuf_t *t, edit_cursor_t to);
void tbuf_edit_end(edit_tbuf_t *t);
void tbuf_search_free(edit_tbuf_t *t);

#endif
