#ifndef EDIT_TBUF_H
#define EDIT_TBUF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "edit/doc.h"
#include "edit/gap.h"
#include "edit/helpers.h"
#include "edit/measure.h"
#include "edit/uregex.h"

#ifdef __cplusplus
extern "C" {
#endif

// Text editor buffer core (cf. Rust buffer::TextBuffer without render/search
// wiring/file IO). Owns a gap buffer plus heap undo/redo stacks.
// Selection model is minimal (anchors only); rich selectors come later.
typedef enum {
    EDIT_MOVE_GRAPHEME,
    EDIT_MOVE_WORD,
} edit_move_t;

typedef struct {
    edit_point_t cursor_before; // logical, before the change
    bool has_selection;
    edit_point_t sel_beg;
    edit_point_t sel_end;
    int32_t logical_before;
    int32_t visual_before;
    uint32_t generation_before;
    edit_point_t cursor; // logical, where the change took place
    uint8_t *deleted;
    size_t deleted_len;
    size_t deleted_cap;
    uint8_t *added;
    size_t added_len;
    size_t added_cap;
} edit_hist_t;

typedef struct {
    bool match_case;
    bool whole_word;
    bool use_regex;
} edit_search_opts_t;

typedef struct {
    char *pattern;
    size_t pattern_len;
    edit_search_opts_t opts;
    edit_doc_t doc; // gap doc backing the UText source (must outlive text)
    edit_utext_t text;
    edit_regex_t regex;
    bool has_text;
    bool has_regex;
    uint32_t buffer_generation;
    uint32_t selection_generation;
    size_t next_search_offset;
    bool no_matches;
} edit_search_t;

typedef struct {
    edit_gap_t buffer;
    edit_hist_t *undo;
    size_t undo_len;
    size_t undo_cap;
    edit_hist_t *redo;
    size_t redo_len;
    size_t redo_cap;
    int hist_last; // 0 other, 1 write, 2 delete
    uint32_t save_generation;
    int32_t edit_depth;
    size_t edit_off;
    bool has_edit_info;
    edit_cursor_t edit_safe_start;
    int32_t edit_line_height;
    size_t edit_next_dist;
    int32_t logical_lines;
    int32_t visual_lines;
    edit_cursor_t cursor;
    bool has_render_cursor;
    edit_cursor_t render_cursor;
    bool has_selection;
    edit_point_t sel_beg;
    edit_point_t sel_end;
    uint32_t sel_generation;
    int32_t width;
    int32_t margin_width;
    bool margin_enabled;
    int32_t wrap_col;
    bool wrap_enabled;
    int32_t tab_size;
    bool indent_tabs;
    bool line_highlight;
    int32_t ruler;
    char encoding[64];
    bool newlines_crlf;
    bool overtype;
    bool wants_visibility;
    edit_search_t *search; // active search cache, NULL when none
} edit_tbuf_t;

int edit_tbuf_init(edit_tbuf_t *t, bool small);
void edit_tbuf_destroy(edit_tbuf_t *t);
size_t edit_tbuf_len(const edit_tbuf_t *t);
int32_t edit_tbuf_logical_lines(const edit_tbuf_t *t);
int32_t edit_tbuf_visual_lines(const edit_tbuf_t *t);
bool edit_tbuf_is_dirty(const edit_tbuf_t *t);
uint32_t edit_tbuf_generation(const edit_tbuf_t *t);
void edit_tbuf_mark_dirty(edit_tbuf_t *t);
const char *edit_tbuf_encoding(const edit_tbuf_t *t);
void edit_tbuf_set_encoding(edit_tbuf_t *t, const char *encoding);
bool edit_tbuf_is_crlf(const edit_tbuf_t *t);
void edit_tbuf_normalize_newlines(edit_tbuf_t *t, bool crlf);
bool edit_tbuf_is_overtype(const edit_tbuf_t *t);
void edit_tbuf_set_overtype(edit_tbuf_t *t, bool overtype);

// Cursor movement (clears selection, breaks history grouping).
void edit_tbuf_goto_offset(edit_tbuf_t *t, size_t offset);
void edit_tbuf_goto_logical(edit_tbuf_t *t, edit_point_t pos);
void edit_tbuf_goto_visual(edit_tbuf_t *t, edit_point_t pos);
void edit_tbuf_move_delta(edit_tbuf_t *t, edit_move_t granularity, int32_t delta);
edit_point_t edit_tbuf_cursor_logical(const edit_tbuf_t *t);
edit_point_t edit_tbuf_cursor_visual(const edit_tbuf_t *t);
size_t edit_tbuf_cursor_offset(const edit_tbuf_t *t);

// Layout config.
int32_t edit_tbuf_margin_width(const edit_tbuf_t *t);
int32_t edit_tbuf_text_width(const edit_tbuf_t *t);
void edit_tbuf_make_visible(edit_tbuf_t *t);
bool edit_tbuf_take_visibility(edit_tbuf_t *t);
void edit_tbuf_set_margin(edit_tbuf_t *t, bool enabled);
bool edit_tbuf_set_width(edit_tbuf_t *t, int32_t width);
int32_t edit_tbuf_tab_size(const edit_tbuf_t *t);
bool edit_tbuf_set_tab_size(edit_tbuf_t *t, int32_t width);
bool edit_tbuf_indent_with_tabs(const edit_tbuf_t *t);
void edit_tbuf_set_indent_tabs(edit_tbuf_t *t, bool enabled);
void edit_tbuf_set_wrap(edit_tbuf_t *t, bool enabled);

// Minimal selection: anchors + ordered range (cursors).
bool edit_tbuf_has_selection(const edit_tbuf_t *t);
void edit_tbuf_clear_selection(edit_tbuf_t *t);
void edit_tbuf_set_selection(edit_tbuf_t *t, edit_point_t beg, edit_point_t end);
bool edit_tbuf_selection_range(edit_tbuf_t *t, edit_cursor_t *out_beg, edit_cursor_t *out_end);
// Same with whole-line fallback when nothing is selected (copy/cut).
bool edit_tbuf_selection_range_fb(edit_tbuf_t *t, bool line_fallback, edit_cursor_t *out_beg,
                                  edit_cursor_t *out_end);
void edit_tbuf_select_word(edit_tbuf_t *t);
void edit_tbuf_select_line(edit_tbuf_t *t);
void edit_tbuf_select_all(edit_tbuf_t *t);
void edit_tbuf_start_selection(edit_tbuf_t *t);
void edit_tbuf_selection_update_visual(edit_tbuf_t *t, edit_point_t pos);
void edit_tbuf_selection_update_logical(edit_tbuf_t *t, edit_point_t pos);
void edit_tbuf_selection_update_delta(edit_tbuf_t *t, edit_move_t granularity, int32_t delta);
// Extracts selection (line fallback); delete removes it (undoable).
// Returns malloc'd bytes (caller frees) and length; NULL/0 when empty.
uint8_t *edit_tbuf_extract_selection(edit_tbuf_t *t, bool del, size_t *out_len);
// Same but refuses search-made selections; *out_has false when refused.
uint8_t *edit_tbuf_extract_user_selection(edit_tbuf_t *t, bool del, size_t *out_len, bool *out_has);

// Content exchange.
void edit_tbuf_copy_from(edit_tbuf_t *t, const edit_doc_t *src);
void edit_tbuf_save_to(edit_tbuf_t *t, edit_doc_t *dst);
void edit_tbuf_read_fwd(const edit_tbuf_t *t, size_t off, const uint8_t **p, size_t *n);
void edit_tbuf_read_bwd(const edit_tbuf_t *t, size_t off, const uint8_t **p, size_t *n);

// Editing with undo grouping. raw skips tab/newline translation.
void edit_tbuf_write(edit_tbuf_t *t, const uint8_t *text, size_t len, bool raw);
void edit_tbuf_delete(edit_tbuf_t *t, edit_move_t granularity, int32_t delta);
void edit_tbuf_undo(edit_tbuf_t *t);
void edit_tbuf_redo(edit_tbuf_t *t);

// First non-whitespace logical position of the cursor line.
edit_point_t edit_tbuf_indent_end(edit_tbuf_t *t);
// Removes one tab-width of indentation (empty selection = no-op, unlike Rust).
void edit_tbuf_unindent(edit_tbuf_t *t);

// File IO over fds (cf. read_file/write_file). Encoding NULL detects via BOM.
// 0 ok, -1 error (see err; Sys(errno) for IO, Icu for conversion).
int edit_tbuf_read_file(edit_tbuf_t *t, int fd, const char *encoding, edit_error_t *err);
int edit_tbuf_write_file(edit_tbuf_t *t, int fd, edit_error_t *err);
// BOM sniff: returns static encoding name or NULL.
const char *edit_bom_detect(const uint8_t *bytes, size_t len);

// Find next occurrence and select it. 0 ok, -1 error (bad pattern/ICU).
int edit_tbuf_find_select(edit_tbuf_t *t, const char *pattern, edit_search_opts_t opts);
int edit_tbuf_find_replace(edit_tbuf_t *t, const char *pattern, edit_search_opts_t opts,
                           const char *replacement);
int edit_tbuf_find_replace_all(edit_tbuf_t *t, const char *pattern, edit_search_opts_t opts,
                               const char *replacement);

#ifdef __cplusplus
}
#endif

#endif
