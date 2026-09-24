#ifndef EDIT_APP_H
#define EDIT_APP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "edit/apperr.h"
#include "edit/helpers.h"
#include "edit/tbuf.h"
#include "edit/tui.h"

#ifdef __cplusplus
extern "C" {
#endif

// Application state + document manager + frame drawing (cf. src/bin/edit).
// Single-threaded; all strings are heap-owned by the app (free on destroy).
// Version baked from Cargo.toml.
#define EDIT_APP_VERSION "1.0.0"

typedef enum {
    EDIT_APP_PICKER_NONE,
    EDIT_APP_PICKER_OPEN,
    EDIT_APP_PICKER_SAVE_AS,
    EDIT_APP_PICKER_SAVE_AS_SHOWN,
} edit_app_picker_t;

typedef enum {
    EDIT_APP_ENC_NONE,
    EDIT_APP_ENC_CONVERT,
    EDIT_APP_ENC_REOPEN,
} edit_app_enc_change_t;

typedef enum {
    EDIT_APP_SEARCH_HIDDEN,
    EDIT_APP_SEARCH_DISABLED,
    EDIT_APP_SEARCH,
    EDIT_APP_SEARCH_REPLACE,
} edit_app_search_kind_t;

typedef struct edit_app_doc {
    struct edit_app_doc *prev;
    struct edit_app_doc *next;
    edit_shared_tbuf_t *buffer; // owned
    char *path;                 // NULL when untitled
    char *dir;                  // display dir, NULL when none
    char *filename;             // never NULL
    uint64_t file_dev;
    uint64_t file_ino;
    bool has_file_id;
    unsigned new_file_counter;
} edit_app_doc_t;

typedef struct {
    edit_app_doc_t *first; // active document
    size_t len;
} edit_app_docs_t;

typedef struct {
    uint32_t menubar_bg;
    uint32_t menubar_fg;
    edit_app_docs_t documents;
    // Ring buffer of the last 10 errors (heap strings).
    char *error_log[10];
    size_t error_idx;
    size_t error_count;
    // File picker.
    edit_app_picker_t picker;
    char *picker_dir;
    char *picker_name;
    char **picker_entries;
    size_t picker_nentries;
    char *picker_overwrite;
    // Search.
    edit_app_search_kind_t search_kind;
    bool search_focus;
    char *search_needle;
    size_t needle_len;
    size_t needle_cap;
    char *search_repl;
    size_t repl_len;
    size_t repl_cap;
    edit_search_opts_t search_opts;
    bool search_success;
    // Dialog flags.
    bool wants_save;
    bool wants_statusbar_focus;
    bool wants_encoding_picker;
    edit_app_enc_change_t wants_encoding_change;
    bool wants_indentation_picker;
    bool wants_document_picker;
    bool wants_about;
    bool wants_close;
    bool wants_exit;
    // OSC 52 clipboard + title tracking.
    char *osc_title_filename;
    uint32_t osc_clip_seen;
    uint32_t osc_clip_send;
    bool osc_clip_always;
    bool exit;
} edit_app_t;

int edit_app_init(edit_app_t *app);
void edit_app_destroy(edit_app_t *app);
// Localized error text into a heap string (caller frees); "" when empty.
char *edit_app_error_text(edit_error_t err);
// Logs an error (ring buffer) + flags rerender; ctx may be NULL.
void edit_app_error(edit_app_t *app, edit_ctx_t *ctx, edit_error_t err);

// Document manager (active = first). 0 ok, -1 error (err set).
edit_app_doc_t *edit_app_active(edit_app_t *app);
int edit_app_add_untitled(edit_app_t *app);
int edit_app_add_file_path(edit_app_t *app, const char *path);
void edit_app_remove_active(edit_app_t *app);
// Moves the first doc matching pred(doc, arg) to front; true if moved.
bool edit_app_update_active(edit_app_t *app, bool (*pred)(const edit_app_doc_t *, const void *),
                            const void *arg);
int edit_app_doc_save(edit_app_t *app, edit_app_doc_t *doc, const char *new_path,
                      edit_error_t *out_err);
int edit_app_doc_reread(edit_app_doc_t *doc, const char *encoding, edit_error_t *out_err);
// Parses "file:line:char" (cf. Rust parse_filename_goto + unit vectors).
// Always sets *out_path (prefix, may equal input); *out_goto valid if true.
bool edit_app_parse_goto(const char *path, const char **out_path, size_t *out_path_len,
                         edit_point_t *out_goto);

// Frame drawing (cf. Rust draw() + draw_*).
void edit_app_setup_colors(edit_app_t *app, edit_tui_t *tui);
void edit_app_draw(edit_ctx_t *ctx, edit_app_t *app);

// CLI: help/version text (snprintf-style, returns needed length excl. NUL).
size_t edit_app_help_text(char *dst, size_t cap);
size_t edit_app_version_text(char *dst, size_t cap);
// Returns 0 ok, -1 error (err set); *out_early_exit when --help/--version.
// Reads cwd + argv[1] + redirected stdin like Rust handle_args.
int edit_app_handle_args(edit_app_t *app, int argc, char **argv, bool *out_early_exit,
                         edit_error_t *err);
// Terminal title + OSC 52 sequences (snprintf-style lengths).
size_t edit_app_title_seq(char *dst, size_t cap, const char *filename);
size_t edit_app_clipboard_seq(char *dst, size_t cap, const uint8_t *clip, size_t clip_len);

#ifdef __cplusplus
}
#endif

#endif
