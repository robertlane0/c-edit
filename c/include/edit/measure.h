#ifndef EDIT_MEASURE_H
#define EDIT_MEASURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "edit/doc.h"
#include "edit/helpers.h"

#ifdef __cplusplus
extern "C" {
#endif

// Text layout measurement (cf. Rust unicode::measurement).
// Coordinate counters use plain i32 arithmetic matching Rust release;
// keep inputs within sane terminal ranges (see COORD_TYPE_SAFE_MAX).
typedef struct {
    size_t offset;        // byte offset
    edit_point_t logical; // graphemes x lines (wrap-independent)
    edit_point_t visual;  // columns x rows (wrap-dependent)
    edit_coord_t column;  // visual columns, wrap-independent
    bool wrap_opp;        // trailing wrap-opportunity state
} edit_cursor_t;

typedef struct {
    const edit_doc_t *doc;
    edit_coord_t tab_size; // >= 1
    edit_coord_t wrap_col; // 0 = no word wrap
    edit_cursor_t cursor;
} edit_measure_t;

void edit_measure_init(edit_measure_t *m, const edit_doc_t *doc);
void edit_measure_set_tab(edit_measure_t *m, edit_coord_t tab_size);
void edit_measure_set_wrap(edit_measure_t *m, edit_coord_t wrap_col);
void edit_measure_set_cursor(edit_measure_t *m, edit_cursor_t cursor);
edit_cursor_t edit_measure_cursor(const edit_measure_t *m);
edit_cursor_t edit_measure_goto_offset(edit_measure_t *m, size_t offset);
edit_cursor_t edit_measure_goto_logical(edit_measure_t *m, edit_point_t target);
edit_cursor_t edit_measure_goto_visual(edit_measure_t *m, edit_point_t target);

// Line-start seekers over byte text (cf. newlines_forward/backward).
// Returns (offset, line reached). Lines count \n only.
void edit_newlines_forward(const uint8_t *text, size_t len, size_t offset, edit_coord_t line,
                           edit_coord_t line_stop, size_t *out_off, edit_coord_t *out_line);
void edit_newlines_backward(const uint8_t *text, size_t len, size_t offset, edit_coord_t line,
                            edit_coord_t line_stop, size_t *out_off, edit_coord_t *out_line);
// Offset past a \r?\n newline at offset (clamped).
size_t edit_skip_newline(const uint8_t *text, size_t len, size_t offset);
// Length of text minus one trailing \r?\n.
size_t edit_strip_newline(const uint8_t *text, size_t len);

#ifdef __cplusplus
}
#endif

#endif
