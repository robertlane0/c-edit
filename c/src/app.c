#define _DEFAULT_SOURCE // O_CLOEXEC, getcwd; must precede all headers

#include "edit/app.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "edit/apploc.h"
#include "edit/base64.h"
#include "edit/fb.h"
#include "edit/helpers.h"
#include "edit/icu.h"
#include "edit/input.h"
#include "edit/oklab.h"
#include "edit/path.h"
#include "edit/simd.h"
#include "edit/tty.h"

// Document defaults shared by new/untitled/file buffers.
static int doc_buffer_defaults(edit_shared_tbuf_t *shared) {
    if (shared == NULL) {
        return -1;
    }
    edit_tbuf_set_margin(&shared->tbuf, true);
    edit_tbuf_set_line_highlight(&shared->tbuf, true);
    return 0;
}

static char *dup_str(const char *s) {
    if (s == NULL) {
        return NULL;
    }
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p != NULL) {
        memcpy(p, s, n);
    }
    return p;
}

int edit_app_init(edit_app_t *app) {
    if (app == NULL) {
        return -1;
    }
    memset(app, 0, sizeof *app);
    app->search_success = true;
    app->search_kind = EDIT_APP_SEARCH_HIDDEN;
    return 0;
}

static void doc_free(edit_app_doc_t *doc) {
    if (doc == NULL) {
        return;
    }
    edit_shared_release(doc->buffer);
    free(doc->path);
    free(doc->dir);
    free(doc->filename);
    free(doc);
}

void edit_app_destroy(edit_app_t *app) {
    if (app == NULL) {
        return;
    }
    edit_app_doc_t *doc = app->documents.first;
    while (doc != NULL) {
        edit_app_doc_t *next = doc->next;
        doc_free(doc);
        doc = next;
    }
    for (int i = 0; i < 10; ++i) {
        free(app->error_log[i]);
    }
    free(app->picker_dir);
    free(app->picker_name);
    if (app->picker_entries != NULL) {
        for (size_t i = 0; i < app->picker_nentries; ++i) {
            free(app->picker_entries[i]);
        }
        free(app->picker_entries);
    }
    free(app->picker_overwrite);
    free(app->search_needle);
    free(app->search_repl);
    free(app->osc_title_filename);
    memset(app, 0, sizeof *app);
}

char *edit_app_error_text(edit_error_t err) {
    const char *msg = "";
    char stack[256];
    if (edit_error_equal(err, EDIT_APP_ICU_MISSING)) {
        msg = edit_loc(EDIT_LOC_ERROR_ICU_MISSING);
        return dup_str(msg);
    }
    if (err.kind == EDIT_ERR_APP) {
        int n = snprintf(stack, sizeof stack, "Unknown app error code: %u", err.code);
        if (n < 0) {
            return dup_str("");
        }
        return dup_str(stack);
    }
    if (err.kind == EDIT_ERR_ICU) {
        size_t n = edit_icu_error_text(err.code, stack, sizeof stack);
        if (n >= sizeof stack) {
            char *p = (char *)malloc(n + 1);
            if (p == NULL) {
                return NULL;
            }
            edit_icu_error_text(err.code, p, n + 1);
            return p;
        }
        return dup_str(stack);
    }
    size_t n = edit_tty_error_text(err.code, stack, sizeof stack);
    if (n >= sizeof stack) {
        char *p = (char *)malloc(n + 1);
        if (p == NULL) {
            return NULL;
        }
        edit_tty_error_text(err.code, p, n + 1);
        return p;
    }
    return dup_str(stack);
}

void edit_app_error(edit_app_t *app, edit_ctx_t *ctx, edit_error_t err) {
    if (app == NULL) {
        return;
    }
    char *msg = edit_app_error_text(err);
    if (msg == NULL || msg[0] == '\0') {
        free(msg);
        return;
    }
    free(app->error_log[app->error_idx]);
    app->error_log[app->error_idx] = msg;
    app->error_idx = (app->error_idx + 1) % 10;
    if (app->error_count < 10) {
        app->error_count += 1;
    }
    if (ctx != NULL) {
        edit_ctx_needs_rerender(ctx);
    }
}

edit_app_doc_t *edit_app_active(edit_app_t *app) {
    if (app == NULL) {
        return NULL;
    }
    return app->documents.first;
}

void edit_app_remove_active(edit_app_t *app) {
    if (app == NULL || app->documents.first == NULL) {
        return;
    }
    edit_app_doc_t *doc = app->documents.first;
    app->documents.first = doc->next;
    if (doc->next != NULL) {
        doc->next->prev = NULL;
    }
    if (app->documents.len > 0) {
        app->documents.len -= 1;
    }
    doc_free(doc);
}

bool edit_app_update_active(edit_app_t *app, bool (*pred)(const edit_app_doc_t *, const void *),
                            const void *arg) {
    if (app == NULL || pred == NULL) {
        return false;
    }
    for (edit_app_doc_t *doc = app->documents.first; doc != NULL; doc = doc->next) {
        if (pred(doc, arg)) {
            if (doc != app->documents.first) {
                // Unlink and move to front.
                if (doc->prev != NULL) {
                    doc->prev->next = doc->next;
                }
                if (doc->next != NULL) {
                    doc->next->prev = doc->prev;
                }
                doc->prev = NULL;
                doc->next = app->documents.first;
                app->documents.first->prev = doc;
                app->documents.first = doc;
            }
            return true;
        }
    }
    return false;
}

// "Untitled-N.txt" with N = max counter + 1 (cf. Rust gen_untitled_name).
static void gen_untitled_name(edit_app_t *app, edit_app_doc_t *doc) {
    unsigned counter = 0;
    for (edit_app_doc_t *d = app->documents.first; d != NULL; d = d->next) {
        if (d->new_file_counter > counter) {
            counter = d->new_file_counter;
        }
    }
    counter += 1;
    char name[64];
    snprintf(name, sizeof name, "Untitled-%u.txt", counter);
    free(doc->filename);
    doc->filename = dup_str(name);
    doc->new_file_counter = counter;
}

int edit_app_add_untitled(edit_app_t *app) {
    if (app == NULL) {
        return -1;
    }
    edit_shared_tbuf_t *shared = NULL;
    if (edit_shared_tbuf_create(&shared, false) != 0) {
        return -1;
    }
    if (doc_buffer_defaults(shared) != 0) {
        edit_shared_release(shared);
        return -1;
    }
    edit_app_doc_t *doc = (edit_app_doc_t *)calloc(1, sizeof *doc);
    if (doc == NULL) {
        edit_shared_release(shared);
        return -1;
    }
    doc->buffer = shared;
    doc->filename = dup_str("");
    if (doc->filename == NULL) {
        doc_free(doc);
        return -1;
    }
    gen_untitled_name(app, doc);
    doc->next = app->documents.first;
    if (app->documents.first != NULL) {
        app->documents.first->prev = doc;
    }
    app->documents.first = doc;
    app->documents.len += 1;
    return 0;
}

// Split path into dir + filename; sets ruler for COMMIT_EDITMSG.
static int doc_set_path(edit_app_doc_t *doc, const char *path) {
    if (doc == NULL || path == NULL) {
        return -1;
    }
    const char *slash = strrchr(path, '/');
    const char *base = slash != NULL ? slash + 1 : path;
    char *filename = dup_str(base);
    char *dir = NULL;
    if (slash != NULL) {
        size_t n = (size_t)(slash - path);
        if (n == 0) {
            n = 1; // root "/"
        }
        dir = (char *)malloc(n + 1);
        if (dir == NULL) {
            free(filename);
            return -1;
        }
        memcpy(dir, path, n);
        dir[n] = '\0';
    } else {
        dir = dup_str("");
        if (dir == NULL) {
            free(filename);
            return -1;
        }
    }
    char *new_path = dup_str(path);
    if (filename == NULL || new_path == NULL) {
        free(filename);
        free(dir);
        free(new_path);
        return -1;
    }
    free(doc->filename);
    free(doc->dir);
    free(doc->path);
    doc->filename = filename;
    doc->dir = dir;
    doc->path = new_path;
    edit_tbuf_set_ruler(&doc->buffer->tbuf, strcmp(filename, "COMMIT_EDITMSG") == 0 ? 72 : 0);
    return 0;
}

int edit_app_doc_save(edit_app_t *app, edit_app_doc_t *doc, const char *new_path,
                      edit_error_t *out_err) {
    if (out_err != NULL) {
        *out_err = edit_error_app(0);
    }
    if (app == NULL || doc == NULL) {
        return -1;
    }
    (void)app;
    const char *path = new_path != NULL ? new_path : doc->path;
    if (path == NULL) {
        return -1;
    }
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
    if (fd < 0) {
        if (out_err != NULL) {
            *out_err = edit_error_sys((uint32_t)errno);
        }
        return -1;
    }
    edit_error_t err = edit_error_app(0);
    int rc = edit_tbuf_write_file(&doc->buffer->tbuf, fd, &err);
    uint64_t dev = 0, ino = 0;
    bool have_id = edit_tty_file_id(fd, &dev, &ino);
    close(fd);
    if (rc != 0) {
        if (out_err != NULL) {
            *out_err = err;
        }
        return -1;
    }
    if (have_id) {
        doc->file_dev = dev;
        doc->file_ino = ino;
        doc->has_file_id = true;
    }
    if (new_path != NULL) {
        if (doc_set_path(doc, new_path) != 0) {
            return -1;
        }
    }
    return 0;
}

int edit_app_doc_reread(edit_app_doc_t *doc, const char *encoding, edit_error_t *out_err) {
    if (out_err != NULL) {
        *out_err = edit_error_app(0);
    }
    if (doc == NULL || doc->path == NULL) {
        return -1;
    }
    int fd = open(doc->path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        if (out_err != NULL) {
            *out_err = edit_error_sys((uint32_t)errno);
        }
        return -1;
    }
    edit_error_t err = edit_error_app(0);
    int rc = edit_tbuf_read_file(&doc->buffer->tbuf, fd, encoding, &err);
    uint64_t dev = 0, ino = 0;
    bool have_id = edit_tty_file_id(fd, &dev, &ino);
    close(fd);
    if (rc != 0) {
        if (out_err != NULL) {
            *out_err = err;
        }
        return -1;
    }
    if (have_id) {
        doc->file_dev = dev;
        doc->file_ino = ino;
        doc->has_file_id = true;
    }
    return 0;
}

// Parses "file:line:char" (cf. Rust DocumentManager::parse_filename_goto).
// Numbers are 1-based in input, 0-based in output (saturating, like Rust).
bool edit_app_parse_goto(const char *path, const char **out_path, size_t *out_path_len,
                         edit_point_t *out_goto) {
    if (out_path != NULL) {
        *out_path = path;
    }
    if (out_path_len != NULL) {
        *out_path_len = path != NULL ? strlen(path) : 0;
    }
    if (path == NULL) {
        return false;
    }
    size_t len = strlen(path);
    const uint8_t *bytes = (const uint8_t *)path;

    size_t colend = 0;
    // Reject empty filenames after stripping (colend > 0 required).
    if (!edit_memrchr2(':', ':', bytes, len, len, &colend) || colend == 0) {
        return false;
    }
    // Parse digits with checked arithmetic (cf. Rust checked_mul/add).
    size_t num = 0;
    bool any = false;
    for (size_t i = colend + 1; i < len; ++i) {
        uint8_t b = bytes[i];
        if (b < '0' || b > '9') {
            return false;
        }
        any = true;
        unsigned digit = (unsigned)(b - (uint8_t)'0');
        if (num > ((size_t)-1 - digit) / 10) {
            return false;
        }
        num = num * 10 + digit;
    }
    if (!any) {
        return false;
    }
    // Rust parses into CoordType (i32) with checked arithmetic.
    if (num > (size_t)INT32_MAX) {
        return false;
    }
    int32_t last = num > 0 ? (int32_t)(num - 1) : 0;
    size_t strip = colend;
    edit_point_t go = {0, last};

    size_t colbeg = 0;
    if (edit_memrchr2(':', ':', bytes, colend, colend, &colbeg) && colbeg != 0) {
        size_t first = 0;
        bool fok = false;
        bool ok = true;
        for (size_t i = colbeg + 1; i < colend; ++i) {
            uint8_t b = bytes[i];
            if (b < '0' || b > '9') {
                ok = false;
                break;
            }
            fok = true;
            unsigned digit = (unsigned)(b - (uint8_t)'0');
            if (first > ((size_t)-1 - digit) / 10) {
                ok = false;
                break;
            }
            first = first * 10 + digit;
        }
        if (ok && fok && first <= (size_t)INT32_MAX) {
            int32_t f = first > 0 ? (int32_t)(first - 1) : 0;
            strip = colbeg;
            go.x = last;
            go.y = f;
        }
    }
    if (out_path_len != NULL) {
        *out_path_len = strip;
    }
    if (out_goto != NULL) {
        *out_goto = go;
    }
    return true;
}

// Predicate arg for update_active: match by (dev, ino).
typedef struct {
    uint64_t dev;
    uint64_t ino;
} file_id_key_t;

static bool doc_has_file_id(const edit_app_doc_t *doc, const void *arg) {
    const file_id_key_t *key = (const file_id_key_t *)arg;
    return doc != NULL && key != NULL && doc->has_file_id && doc->file_dev == key->dev &&
           doc->file_ino == key->ino;
}

int edit_app_add_file_path(edit_app_t *app, const char *path) {
    if (app == NULL || path == NULL) {
        return -1;
    }
    // Normalize into a heap buffer (normalize reports needed length).
    size_t need = edit_path_normalize(NULL, 0, path);
    char *norm = (char *)malloc(need + 1);
    if (norm == NULL) {
        return -1;
    }
    edit_path_normalize(norm, need + 1, path);

    const char *stripped = norm;
    size_t stripped_len = need;
    edit_point_t go = {0, 0};
    bool has_goto = edit_app_parse_goto(norm, &stripped, &stripped_len, &go);
    // Copy the stripped prefix (parse works on byte offsets).
    char *file = (char *)malloc(stripped_len + 1);
    if (file == NULL) {
        free(norm);
        return -1;
    }
    memcpy(file, stripped, stripped_len);
    file[stripped_len] = '\0';
    free(norm);

    int fd = open(file, O_RDONLY | O_CLOEXEC);
    if (fd < 0 && errno != ENOENT) {
        int saved = errno;
        free(file);
        errno = saved;
        return -1;
    }
    uint64_t dev = 0, ino = 0;
    bool have_id = fd >= 0 && edit_tty_file_id(fd, &dev, &ino);

    // Already open? Focus it (and jump) instead.
    if (have_id) {
        file_id_key_t key = {dev, ino};
        if (edit_app_update_active(app, doc_has_file_id, &key)) {
            if (fd >= 0) {
                close(fd);
            }
            free(file);
            edit_app_doc_t *doc = app->documents.first;
            if (has_goto && doc != NULL) {
                edit_tbuf_goto_logical(&doc->buffer->tbuf, go);
            }
            return 0;
        }
    }

    edit_shared_tbuf_t *shared = NULL;
    if (edit_shared_tbuf_create(&shared, false) != 0) {
        if (fd >= 0) {
            close(fd);
        }
        free(file);
        return -1;
    }
    if (doc_buffer_defaults(shared) != 0) {
        edit_shared_release(shared);
        if (fd >= 0) {
            close(fd);
        }
        free(file);
        return -1;
    }
    if (fd >= 0) {
        edit_error_t err = edit_error_app(0);
        int rc = edit_tbuf_read_file(&shared->tbuf, fd, NULL, &err);
        close(fd);
        if (rc != 0) {
            edit_shared_release(shared);
            free(file);
            return -1;
        }
        if (has_goto && (go.x != 0 || go.y != 0)) {
            edit_tbuf_goto_logical(&shared->tbuf, go);
        }
    }

    edit_app_doc_t *doc = (edit_app_doc_t *)calloc(1, sizeof *doc);
    if (doc == NULL) {
        edit_shared_release(shared);
        free(file);
        return -1;
    }
    doc->buffer = shared;
    doc->filename = dup_str("");
    if (doc->filename == NULL) {
        doc_free(doc);
        free(file);
        return -1;
    }
    if (have_id) {
        doc->file_dev = dev;
        doc->file_ino = ino;
        doc->has_file_id = true;
    }
    if (doc_set_path(doc, file) != 0) {
        doc_free(doc);
        free(file);
        return -1;
    }
    free(file);
    doc->next = app->documents.first;
    if (app->documents.first != NULL) {
        app->documents.first->prev = doc;
    }
    app->documents.first = doc;
    app->documents.len += 1;
    return 0;
}

// ---- Frame drawing (cf. src/bin/edit/draw_*) ----

static edit_rect_t pad2(int32_t tb, int32_t lr) {
    edit_rect_t r = {lr, tb, lr, tb};
    return r;
}

static edit_rect_t pad3(int32_t top, int32_t lr, int32_t bottom) {
    edit_rect_t r = {lr, top, lr, bottom};
    return r;
}

void edit_app_setup_colors(edit_app_t *app, edit_tui_t *tui) {
    if (app == NULL || tui == NULL) {
        return;
    }
    uint32_t bg = edit_tui_indexed(tui, EDIT_FB_BACKGROUND);
    uint32_t blue = edit_tui_indexed_alpha(tui, EDIT_FB_BRIGHT_BLUE, 1, 2);
    app->menubar_bg = edit_oklab_blend(bg, blue);
    app->menubar_fg = edit_tui_contrasted(tui, app->menubar_bg);
    uint32_t fbg = edit_tui_indexed_alpha(tui, EDIT_FB_BACKGROUND, 2, 3);
    uint32_t ffg = edit_tui_indexed_alpha(tui, EDIT_FB_FOREGROUND, 1, 3);
    uint32_t floater_bg = edit_oklab_blend(fbg, ffg);
    uint32_t floater_fg = edit_tui_contrasted(tui, floater_bg);
    tui->floater_bg = floater_bg;
    tui->floater_fg = floater_fg;
    tui->modal_bg = floater_bg;
    tui->modal_fg = floater_fg;
}

static void draw_add_untitled(edit_ctx_t *ctx, edit_app_t *app) {
    if (edit_app_add_untitled(app) != 0) {
        edit_app_error(app, ctx, edit_error_app(1));
    }
}

static void edit_app_draw_menubar(edit_ctx_t *ctx, edit_app_t *app);

static void draw_menu_file(edit_ctx_t *ctx, edit_app_t *app) {
    if (edit_ctx_menubar_menu_button(ctx, edit_loc(EDIT_LOC_FILE_NEW), 'N',
                                     (uint32_t)EDIT_KBMOD_CTRL | (uint32_t)'N')) {
        draw_add_untitled(ctx, app);
    }
    if (edit_ctx_menubar_menu_button(ctx, edit_loc(EDIT_LOC_FILE_OPEN), 'O',
                                     (uint32_t)EDIT_KBMOD_CTRL | (uint32_t)'O')) {
        app->picker = EDIT_APP_PICKER_OPEN;
    }
    if (edit_app_active(app) != NULL) {
        if (edit_ctx_menubar_menu_button(ctx, edit_loc(EDIT_LOC_FILE_SAVE), 'S',
                                         (uint32_t)EDIT_KBMOD_CTRL | (uint32_t)'S')) {
            app->wants_save = true;
        }
        if (edit_ctx_menubar_menu_button(ctx, edit_loc(EDIT_LOC_FILE_SAVE_AS), 'A', 0)) {
            app->picker = EDIT_APP_PICKER_SAVE_AS;
        }
    }
    if (edit_ctx_menubar_menu_button(ctx, edit_loc(EDIT_LOC_FILE_CLOSE), 'C',
                                     (uint32_t)EDIT_KBMOD_CTRL | (uint32_t)'W')) {
        app->wants_close = true;
    }
    if (edit_ctx_menubar_menu_button(ctx, edit_loc(EDIT_LOC_FILE_EXIT), 'X',
                                     (uint32_t)EDIT_KBMOD_CTRL | (uint32_t)'Q')) {
        app->wants_exit = true;
    }
    edit_ctx_menubar_menu_end(ctx);
}

static void draw_menu_edit(edit_ctx_t *ctx, edit_app_t *app) {
    edit_app_doc_t *doc = edit_app_active(app);
    if (doc == NULL) {
        edit_ctx_menubar_menu_end(ctx);
        return;
    }
    edit_tbuf_t *tb = &doc->buffer->tbuf;
    if (edit_ctx_menubar_menu_button(ctx, edit_loc(EDIT_LOC_EDIT_UNDO), 'U',
                                     (uint32_t)EDIT_KBMOD_CTRL | (uint32_t)'Z')) {
        edit_tbuf_undo(tb);
        edit_ctx_needs_rerender(ctx);
    }
    if (edit_ctx_menubar_menu_button(ctx, edit_loc(EDIT_LOC_EDIT_REDO), 'R',
                                     (uint32_t)EDIT_KBMOD_CTRL | (uint32_t)'Y')) {
        edit_tbuf_redo(tb);
        edit_ctx_needs_rerender(ctx);
    }
    if (edit_ctx_menubar_menu_button(ctx, edit_loc(EDIT_LOC_EDIT_CUT), 'T',
                                     (uint32_t)EDIT_KBMOD_CTRL | (uint32_t)'X')) {
        size_t n = 0;
        uint8_t *sel = edit_tbuf_extract_selection(tb, true, &n);
        if (sel != NULL) {
            edit_ctx_set_clipboard(ctx, sel, n);
            free(sel);
        }
    }
    if (edit_ctx_menubar_menu_button(ctx, edit_loc(EDIT_LOC_EDIT_COPY), 'C',
                                     (uint32_t)EDIT_KBMOD_CTRL | (uint32_t)'C')) {
        size_t n = 0;
        uint8_t *sel = edit_tbuf_extract_selection(tb, false, &n);
        if (sel != NULL) {
            edit_ctx_set_clipboard(ctx, sel, n);
            free(sel);
        }
    }
    if (edit_ctx_menubar_menu_button(ctx, edit_loc(EDIT_LOC_EDIT_PASTE), 'P',
                                     (uint32_t)EDIT_KBMOD_CTRL | (uint32_t)'V')) {
        const uint8_t *cb = NULL;
        size_t n = edit_ctx_clipboard(ctx, &cb);
        if (cb != NULL && n > 0) {
            edit_tbuf_write(tb, cb, n, true);
        }
        edit_ctx_needs_rerender(ctx);
    }
    if (app->search_kind != EDIT_APP_SEARCH_DISABLED) {
        if (edit_ctx_menubar_menu_button(ctx, edit_loc(EDIT_LOC_EDIT_FIND), 'F',
                                         (uint32_t)EDIT_KBMOD_CTRL | (uint32_t)'F')) {
            app->search_kind = EDIT_APP_SEARCH;
            app->search_focus = true;
        }
        if (edit_ctx_menubar_menu_button(ctx, edit_loc(EDIT_LOC_EDIT_REPLACE), 'R',
                                         (uint32_t)EDIT_KBMOD_CTRL | (uint32_t)'R')) {
            app->search_kind = EDIT_APP_SEARCH_REPLACE;
            app->search_focus = true;
        }
    }
    edit_ctx_menubar_menu_end(ctx);
}

static void draw_menu_view(edit_ctx_t *ctx, edit_app_t *app) {
    if (edit_ctx_menubar_menu_button(ctx, edit_loc(EDIT_LOC_VIEW_FOCUS_STATUSBAR), 'S', 0)) {
        app->wants_statusbar_focus = true;
    }
    edit_app_doc_t *doc = edit_app_active(app);
    if (doc != NULL) {
        edit_tbuf_t *tb = &doc->buffer->tbuf;
        bool wrap = edit_tbuf_is_wrap(tb);
        if (edit_ctx_menubar_menu_checkbox(ctx, edit_loc(EDIT_LOC_VIEW_WORD_WRAP), 'W',
                                           (uint32_t)EDIT_KBMOD_ALT | (uint32_t)'Z', wrap)) {
            edit_tbuf_set_wrap(tb, !wrap);
            edit_ctx_needs_rerender(ctx);
        }
    }
    edit_ctx_menubar_menu_end(ctx);
}

static void draw_menu_help(edit_ctx_t *ctx, edit_app_t *app) {
    if (edit_ctx_menubar_menu_button(ctx, edit_loc(EDIT_LOC_HELP_ABOUT), 'A', 0)) {
        app->wants_about = true;
    }
    edit_ctx_menubar_menu_end(ctx);
}

static void edit_app_draw_menubar(edit_ctx_t *ctx, edit_app_t *app) {
    if (ctx == NULL || app == NULL) {
        return;
    }
    edit_ctx_menubar_begin(ctx);
    edit_ctx_attr_bg(ctx, app->menubar_bg);
    edit_ctx_attr_fg(ctx, app->menubar_fg);
    if (edit_ctx_menubar_menu_begin(ctx, edit_loc(EDIT_LOC_FILE), 'F')) {
        draw_menu_file(ctx, app);
    }
    if (edit_app_active(app) != NULL &&
        edit_ctx_menubar_menu_begin(ctx, edit_loc(EDIT_LOC_EDIT), 'E')) {
        draw_menu_edit(ctx, app);
    }
    if (edit_ctx_menubar_menu_begin(ctx, edit_loc(EDIT_LOC_VIEW), 'V')) {
        draw_menu_view(ctx, app);
    }
    if (edit_ctx_menubar_menu_begin(ctx, edit_loc(EDIT_LOC_HELP), 'H')) {
        draw_menu_help(ctx, app);
    }
    edit_ctx_menubar_end(ctx);
}

static void draw_dialog_about(edit_ctx_t *ctx, edit_app_t *app) {
    edit_ctx_modal_begin(ctx, "about", edit_loc(EDIT_LOC_ABOUT_DIALOG_TITLE));
    edit_ctx_block_begin(ctx, "content");
    edit_ctx_inherit_focus(ctx);
    edit_ctx_attr_padding(ctx, pad3(1, 2, 1));
    edit_ctx_label(ctx, "description", "Microsoft Edit");
    edit_ctx_set_overflow(ctx, 3);
    edit_ctx_attr_position(ctx, EDIT_POS_CENTER);
    char ver[128];
    snprintf(ver, sizeof ver, "%s%s", edit_loc(EDIT_LOC_ABOUT_DIALOG_VERSION), EDIT_APP_VERSION);
    edit_ctx_label(ctx, "version", ver);
    edit_ctx_set_overflow(ctx, 1);
    edit_ctx_attr_position(ctx, EDIT_POS_CENTER);
    edit_ctx_label(ctx, "copyright", "Copyright (c) Microsoft Corp 2025");
    edit_ctx_set_overflow(ctx, 3);
    edit_ctx_attr_position(ctx, EDIT_POS_CENTER);
    edit_ctx_block_begin(ctx, "choices");
    edit_ctx_inherit_focus(ctx);
    edit_ctx_attr_padding(ctx, pad3(1, 2, 0));
    edit_ctx_attr_position(ctx, EDIT_POS_CENTER);
    if (edit_ctx_button(ctx, "ok", edit_loc(EDIT_LOC_OK))) {
        app->wants_about = false;
    }
    edit_ctx_inherit_focus(ctx);
    edit_ctx_block_end(ctx);
    edit_ctx_block_end(ctx);
    if (edit_ctx_modal_end(ctx)) {
        app->wants_about = false;
    }
}

static void draw_error_log(edit_ctx_t *ctx, edit_app_t *app) {
    edit_ctx_modal_begin(ctx, "error", edit_loc(EDIT_LOC_ERROR_DIALOG_TITLE));
    edit_ctx_attr_bg(ctx, edit_tui_indexed(ctx->tui, EDIT_FB_RED));
    edit_ctx_attr_fg(ctx, edit_tui_indexed(ctx->tui, EDIT_FB_BRIGHT_WHITE));
    edit_ctx_block_begin(ctx, "content");
    edit_ctx_attr_padding(ctx, pad3(0, 2, 1));
    size_t off = app->error_idx + 10 - app->error_count;
    for (size_t i = 0; i < app->error_count; ++i) {
        size_t idx = (off + i) % 10;
        const char *msg = app->error_log[idx];
        if (msg != NULL && msg[0] != '\0') {
            edit_ctx_id_mixin(ctx, (uint64_t)i);
            edit_ctx_label(ctx, "error", msg);
            edit_ctx_set_overflow(ctx, 3);
        }
    }
    edit_ctx_block_end(ctx);
    if (edit_ctx_button(ctx, "ok", edit_loc(EDIT_LOC_OK))) {
        app->error_count = 0;
    }
    edit_ctx_attr_position(ctx, EDIT_POS_CENTER);
    edit_ctx_inherit_focus(ctx);
    if (edit_ctx_modal_end(ctx)) {
        app->error_count = 0;
    }
}

static void draw_editor_search(edit_ctx_t *ctx, edit_app_t *app) {
    if (!edit_icu_available()) {
        edit_app_error(app, ctx, EDIT_APP_ICU_MISSING);
        app->search_kind = EDIT_APP_SEARCH_DISABLED;
        return;
    }
    edit_app_doc_t *doc = edit_app_active(app);
    if (doc == NULL) {
        app->search_kind = EDIT_APP_SEARCH_HIDDEN;
        return;
    }
    edit_tbuf_t *tb = &doc->buffer->tbuf;
    // 0 none, 1 search, 2 replace, 3 replace-all.
    int action = 0;
    int focus = EDIT_APP_SEARCH_HIDDEN;

    if (app->search_focus) {
        app->search_focus = false;
        focus = EDIT_APP_SEARCH;
        size_t n = 0;
        bool has = false;
        uint8_t *sel = edit_tbuf_extract_user_selection(tb, false, &n, &has);
        if (has && sel != NULL) {
            free(app->search_needle);
            app->search_needle = (char *)malloc(n + 1);
            if (app->search_needle != NULL) {
                memcpy(app->search_needle, sel, n);
                app->search_needle[n] = '\0';
                app->needle_len = n;
                app->needle_cap = n + 1;
            } else {
                app->needle_len = 0;
                app->needle_cap = 0;
            }
            free(sel);
            focus = app->search_kind;
        } else {
            free(sel);
        }
    }

    edit_ctx_block_begin(ctx, "search");
    edit_ctx_attr_focus_well(ctx);
    edit_ctx_attr_bg(ctx, edit_tui_indexed(ctx->tui, EDIT_FB_WHITE));
    edit_ctx_attr_fg(ctx, edit_tui_indexed(ctx->tui, EDIT_FB_BLACK));
    if (edit_ctx_contains_focus(ctx) && edit_ctx_consume_shortcut(ctx, EDIT_VK_ESCAPE)) {
        app->search_kind = EDIT_APP_SEARCH_HIDDEN;
    }
    edit_ctx_table_begin(ctx, "needle");
    edit_size_t gap1 = {1, 0};
    edit_ctx_table_set_gap(ctx, gap1);
    edit_ctx_table_next_row(ctx);
    edit_ctx_label(ctx, "label", edit_loc(EDIT_LOC_SEARCH_NEEDLE_LABEL));
    if (edit_ctx_editline_str(ctx, "needle", &app->search_needle, &app->needle_len,
                              &app->needle_cap)) {
        action = 1;
    }
    if (!app->search_success) {
        edit_ctx_attr_bg(ctx, edit_tui_indexed(ctx->tui, EDIT_FB_RED));
        edit_ctx_attr_fg(ctx, edit_tui_indexed(ctx->tui, EDIT_FB_BRIGHT_WHITE));
    }
    edit_size_t wide = {EDIT_COORD_SAFE_MAX, 1};
    edit_ctx_attr_intrinsic_size(ctx, wide);
    if (focus == EDIT_APP_SEARCH) {
        edit_ctx_steal_focus(ctx);
    }
    if (edit_ctx_is_focused(ctx) && edit_ctx_consume_shortcut(ctx, EDIT_VK_RETURN)) {
        action = 1;
    }
    if (app->search_kind == EDIT_APP_SEARCH_REPLACE) {
        edit_ctx_table_next_row(ctx);
        edit_ctx_label(ctx, "label", edit_loc(EDIT_LOC_SEARCH_REPLACEMENT_LABEL));
        edit_ctx_editline_str(ctx, "replacement", &app->search_repl, &app->repl_len,
                              &app->repl_cap);
        edit_ctx_attr_intrinsic_size(ctx, wide);
        if (focus == EDIT_APP_SEARCH_REPLACE) {
            edit_ctx_steal_focus(ctx);
        }
        if (edit_ctx_is_focused(ctx)) {
            if (edit_ctx_consume_shortcut(ctx, EDIT_VK_RETURN)) {
                action = 2;
            } else if (edit_ctx_consume_shortcut(ctx, (uint32_t)EDIT_KBMOD_CTRL |
                                                          (uint32_t)EDIT_KBMOD_ALT |
                                                          (uint32_t)EDIT_VK_RETURN)) {
                action = 3;
            }
        }
    }
    edit_ctx_table_end(ctx);

    edit_ctx_table_begin(ctx, "options");
    edit_size_t gap2 = {2, 0};
    edit_ctx_table_set_gap(ctx, gap2);
    edit_ctx_table_next_row(ctx);
    bool change = false;
    if (edit_ctx_checkbox(ctx, "match-case", edit_loc(EDIT_LOC_SEARCH_MATCH_CASE),
                          &app->search_opts.match_case)) {
        change = true;
    }
    if (edit_ctx_checkbox(ctx, "whole-word", edit_loc(EDIT_LOC_SEARCH_WHOLE_WORD),
                          &app->search_opts.whole_word)) {
        change = true;
    }
    if (edit_ctx_checkbox(ctx, "use-regex", edit_loc(EDIT_LOC_SEARCH_USE_REGEX),
                          &app->search_opts.use_regex)) {
        change = true;
    }
    if (change) {
        action = 1;
        app->search_focus = true;
        edit_ctx_needs_rerender(ctx);
    }
    if (app->search_kind == EDIT_APP_SEARCH_REPLACE &&
        edit_ctx_button(ctx, "replace-all", edit_loc(EDIT_LOC_SEARCH_REPLACE_ALL))) {
        action = 3;
    }
    if (edit_ctx_button(ctx, "close", edit_loc(EDIT_LOC_SEARCH_CLOSE))) {
        app->search_kind = EDIT_APP_SEARCH_HIDDEN;
    }
    edit_ctx_table_end(ctx);
    edit_ctx_block_end(ctx);

    if (action == 0) {
        return;
    }
    const char *needle = app->search_needle != NULL ? app->search_needle : "";
    const char *repl = app->search_repl != NULL ? app->search_repl : "";
    int rc = -1;
    if (action == 1) {
        rc = edit_tbuf_find_select(tb, needle, app->search_opts);
    } else if (action == 2) {
        rc = edit_tbuf_find_replace(tb, needle, app->search_opts, repl);
    } else {
        rc = edit_tbuf_find_replace_all(tb, needle, app->search_opts, repl);
    }
    app->search_success = rc == 0;
    edit_ctx_needs_rerender(ctx);
}

static void draw_editor(edit_ctx_t *ctx, edit_app_t *app) {
    if (app->search_kind != EDIT_APP_SEARCH_HIDDEN &&
        app->search_kind != EDIT_APP_SEARCH_DISABLED) {
        draw_editor_search(ctx, app);
    }
    edit_size_t size = edit_tui_size(ctx->tui);
    int32_t reduction = 2;
    if (app->search_kind == EDIT_APP_SEARCH) {
        reduction = 4;
    } else if (app->search_kind == EDIT_APP_SEARCH_REPLACE) {
        reduction = 5;
    }
    edit_app_doc_t *doc = edit_app_active(app);
    if (doc != NULL) {
        edit_ctx_textarea(ctx, "textarea", doc->buffer);
        edit_ctx_inherit_focus(ctx);
    } else {
        edit_ctx_block_begin(ctx, "empty");
        edit_ctx_block_end(ctx);
    }
    edit_size_t isz = {0, size.height - reduction};
    edit_ctx_attr_intrinsic_size(ctx, isz);
}

static void draw_handle_save(edit_ctx_t *ctx, edit_app_t *app) {
    edit_app_doc_t *doc = edit_app_active(app);
    if (doc != NULL) {
        if (doc->path != NULL) {
            edit_error_t err = edit_error_app(0);
            if (edit_app_doc_save(app, doc, NULL, &err) != 0) {
                edit_app_error(app, ctx, err);
            }
        } else {
            app->picker = EDIT_APP_PICKER_SAVE_AS;
            app->wants_save = false;
            edit_ctx_needs_rerender(ctx);
        }
    }
    app->wants_save = false;
}

static void draw_handle_wants_close(edit_ctx_t *ctx, edit_app_t *app) {
    edit_app_doc_t *doc = edit_app_active(app);
    if (doc == NULL) {
        app->wants_close = false;
        app->wants_exit = true;
        return;
    }
    if (!edit_tbuf_is_dirty(&doc->buffer->tbuf)) {
        edit_app_remove_active(app);
        app->wants_close = false;
        edit_ctx_needs_rerender(ctx);
        return;
    }
    // 0 none, 1 save, 2 discard, 3 cancel.
    int action = 0;
    edit_ctx_modal_begin(ctx, "unsaved-changes", edit_loc(EDIT_LOC_UNSAVED_CHANGES_DIALOG_TITLE));
    edit_ctx_attr_bg(ctx, edit_tui_indexed(ctx->tui, EDIT_FB_RED));
    edit_ctx_attr_fg(ctx, edit_tui_indexed(ctx->tui, EDIT_FB_BRIGHT_WHITE));
    edit_ctx_label(ctx, "description", edit_loc(EDIT_LOC_UNSAVED_CHANGES_DIALOG_DESCRIPTION));
    edit_ctx_attr_padding(ctx, pad3(1, 2, 1));
    edit_ctx_table_begin(ctx, "choices");
    edit_ctx_inherit_focus(ctx);
    edit_ctx_attr_padding(ctx, pad3(0, 2, 1));
    edit_ctx_attr_position(ctx, EDIT_POS_CENTER);
    edit_size_t tgap = {2, 0};
    edit_ctx_table_set_gap(ctx, tgap);
    edit_ctx_table_next_row(ctx);
    edit_ctx_inherit_focus(ctx);
    if (edit_ctx_button(ctx, "yes", edit_loc(EDIT_LOC_UNSAVED_CHANGES_DIALOG_YES))) {
        action = 1;
    }
    edit_ctx_inherit_focus(ctx);
    if (edit_ctx_button(ctx, "no", edit_loc(EDIT_LOC_UNSAVED_CHANGES_DIALOG_NO))) {
        action = 2;
    }
    if (edit_ctx_button(ctx, "cancel", edit_loc(EDIT_LOC_CANCEL))) {
        action = 3;
    }
    if (edit_ctx_consume_shortcut(ctx, (uint32_t)'S')) {
        action = 1;
    } else if (edit_ctx_consume_shortcut(ctx, (uint32_t)'N')) {
        action = 2;
    }
    edit_ctx_table_end(ctx);
    if (edit_ctx_modal_end(ctx)) {
        action = 3;
    }
    if (action == 0) {
        return;
    }
    if (action == 1) {
        app->wants_save = true;
    } else if (action == 2) {
        edit_app_remove_active(app);
    } else {
        app->wants_exit = false;
    }
    app->wants_close = false;
    edit_ctx_toss_focus_up(ctx);
}

static void draw_statusbar(edit_ctx_t *ctx, edit_app_t *app) {
    edit_ctx_table_begin(ctx, "statusbar");
    edit_ctx_attr_focus_well(ctx);
    edit_ctx_attr_bg(ctx, app->menubar_bg);
    edit_ctx_attr_fg(ctx, app->menubar_fg);
    edit_size_t tgap = {2, 0};
    edit_ctx_table_set_gap(ctx, tgap);
    edit_size_t isz = {EDIT_COORD_SAFE_MAX, 1};
    edit_ctx_attr_intrinsic_size(ctx, isz);
    edit_ctx_attr_padding(ctx, pad2(0, 1));

    edit_app_doc_t *doc = edit_app_active(app);
    if (doc != NULL) {
        edit_tbuf_t *tb = &doc->buffer->tbuf;
        edit_ctx_table_next_row(ctx);
        if (edit_ctx_button(ctx, "newline", edit_tbuf_is_crlf(tb) ? "CRLF" : "LF")) {
            edit_tbuf_normalize_newlines(tb, !edit_tbuf_is_crlf(tb));
        }
        if (app->wants_statusbar_focus) {
            app->wants_statusbar_focus = false;
            edit_ctx_steal_focus(ctx);
        }
        if (edit_ctx_button(ctx, "encoding", edit_tbuf_encoding(tb))) {
            app->wants_encoding_picker = true;
        }
        if (app->wants_encoding_picker) {
            if (doc->path != NULL) {
                edit_ctx_block_begin(ctx, "frame");
                edit_ctx_attr_float(ctx, 0, 0.0f, 1.0f, 0.0f, 0.0f);
                edit_ctx_attr_padding(ctx, pad2(0, 1));
                edit_ctx_attr_border(ctx);
                if (edit_ctx_button(ctx, "reopen", edit_loc(EDIT_LOC_ENCODING_REOPEN))) {
                    app->wants_encoding_change = EDIT_APP_ENC_REOPEN;
                }
                edit_ctx_focus_on_first_present(ctx);
                if (edit_ctx_button(ctx, "convert", edit_loc(EDIT_LOC_ENCODING_CONVERT))) {
                    app->wants_encoding_change = EDIT_APP_ENC_CONVERT;
                }
                edit_ctx_block_end(ctx);
            } else {
                app->wants_encoding_change = EDIT_APP_ENC_CONVERT;
            }
            if (!edit_ctx_contains_focus(ctx)) {
                app->wants_encoding_picker = false;
                edit_ctx_needs_rerender(ctx);
            }
        }

        char indent[64];
        snprintf(indent, sizeof indent, "%s:%d",
                 edit_tbuf_indent_with_tabs(tb) ? edit_loc(EDIT_LOC_INDENTATION_TABS)
                                                : edit_loc(EDIT_LOC_INDENTATION_SPACES),
                 edit_tbuf_tab_size(tb));
        if (edit_ctx_button(ctx, "indentation", indent)) {
            app->wants_indentation_picker = true;
        }
        if (app->wants_indentation_picker) {
            edit_ctx_table_begin(ctx, "indentation-picker");
            edit_ctx_attr_float(ctx, 0, 0.0f, 1.0f, 0.0f, 0.0f);
            edit_ctx_attr_border(ctx);
            edit_ctx_attr_padding(ctx, pad2(0, 1));
            edit_size_t igap = {1, 0};
            edit_ctx_table_set_gap(ctx, igap);
            if (edit_ctx_consume_shortcut(ctx, EDIT_VK_RETURN)) {
                edit_ctx_toss_focus_up(ctx);
            }
            edit_ctx_table_next_row(ctx);
            edit_ctx_list_begin(ctx, "type");
            edit_ctx_focus_on_first_present(ctx);
            edit_ctx_attr_padding(ctx, pad2(0, 1));
            if (edit_ctx_list_item(ctx, edit_tbuf_indent_with_tabs(tb),
                                   edit_loc(EDIT_LOC_INDENTATION_TABS)) != 0) {
                edit_tbuf_set_indent_tabs(tb, true);
                edit_ctx_needs_rerender(ctx);
            }
            if (edit_ctx_list_item(ctx, !edit_tbuf_indent_with_tabs(tb),
                                   edit_loc(EDIT_LOC_INDENTATION_SPACES)) != 0) {
                edit_tbuf_set_indent_tabs(tb, false);
                edit_ctx_needs_rerender(ctx);
            }
            edit_ctx_list_end(ctx);
            edit_ctx_list_begin(ctx, "width");
            edit_ctx_attr_padding(ctx, pad2(0, 2));
            for (int width = 1; width <= 8; ++width) {
                char digit[2] = {(char)('0' + width), '\0'};
                if (edit_ctx_list_item(ctx, edit_tbuf_tab_size(tb) == width, digit) != 0) {
                    edit_tbuf_set_tab_size(tb, width);
                    edit_ctx_needs_rerender(ctx);
                }
            }
            edit_ctx_list_end(ctx);
            edit_ctx_table_end(ctx);
            if (!edit_ctx_contains_focus(ctx)) {
                app->wants_indentation_picker = false;
                edit_ctx_needs_rerender(ctx);
            }
        }

        edit_point_t cur = edit_tbuf_cursor_logical(tb);
        char location[64];
        snprintf(location, sizeof location, "%d:%d", cur.y + 1, cur.x + 1);
        edit_ctx_label(ctx, "location", location);

        if (edit_tbuf_is_overtype(tb) && edit_ctx_button(ctx, "overtype", "OVR")) {
            edit_tbuf_set_overtype(tb, false);
            edit_ctx_needs_rerender(ctx);
        }
        if (edit_tbuf_is_dirty(tb)) {
            edit_ctx_label(ctx, "dirty", "*");
        }

        edit_ctx_block_begin(ctx, "filename-container");
        edit_size_t fsz = {EDIT_COORD_SAFE_MAX, 1};
        edit_ctx_attr_intrinsic_size(ctx, fsz);
        const char *filename = doc->filename != NULL ? doc->filename : "";
        char namebuf[512];
        if (app->documents.len > 1) {
            snprintf(namebuf, sizeof namebuf, "%s + %zu", filename, app->documents.len - 1);
            filename = namebuf;
        }
        if (edit_ctx_button(ctx, "filename", filename)) {
            app->wants_document_picker = true;
        }
        edit_ctx_inherit_focus(ctx);
        edit_ctx_set_overflow(ctx, 2);
        edit_ctx_attr_position(ctx, EDIT_POS_RIGHT);
        edit_ctx_block_end(ctx);
    }
    edit_ctx_table_end(ctx);
}

static void draw_dialog_encoding_change(edit_ctx_t *ctx, edit_app_t *app) {
    edit_app_doc_t *doc = edit_app_active(app);
    if (doc == NULL) {
        app->wants_encoding_change = EDIT_APP_ENC_NONE;
        return;
    }
    bool reopen = app->wants_encoding_change == EDIT_APP_ENC_REOPEN;
    edit_size_t size = edit_tui_size(ctx->tui);
    int32_t width = size.width - 20;
    int32_t height = size.height - 10;
    if (width < 10) {
        width = 10;
    }
    if (height < 10) {
        height = 10;
    }
    const char *change = NULL;
    edit_ctx_modal_begin(ctx, "encode",
                         reopen ? edit_loc(EDIT_LOC_ENCODING_REOPEN)
                                : edit_loc(EDIT_LOC_ENCODING_CONVERT));
    edit_size_t isz = {width, height};
    edit_ctx_scrollarea_begin(ctx, "scrollarea", isz);
    edit_ctx_attr_bg(ctx, edit_tui_indexed_alpha(ctx->tui, EDIT_FB_BLACK, 1, 4));
    edit_ctx_inherit_focus(ctx);
    const char **encodings = NULL;
    size_t nenc = edit_icu_encodings(&encodings);
    edit_ctx_list_begin(ctx, "encodings");
    edit_ctx_inherit_focus(ctx);
    const char *current = edit_tbuf_encoding(&doc->buffer->tbuf);
    for (size_t i = 0; i < nenc; ++i) {
        if (edit_ctx_list_item(ctx, strcmp(encodings[i], current) == 0, encodings[i]) == 2) {
            change = encodings[i];
            break;
        }
    }
    edit_ctx_list_end(ctx);
    edit_ctx_scrollarea_end(ctx);
    if (edit_ctx_modal_end(ctx)) {
        app->wants_encoding_change = EDIT_APP_ENC_NONE;
    }
    if (change != NULL) {
        if (reopen && doc->path != NULL) {
            edit_error_t err = edit_error_app(0);
            int rc = 0;
            if (edit_tbuf_is_dirty(&doc->buffer->tbuf)) {
                rc = edit_app_doc_save(app, doc, NULL, &err);
            }
            if (rc == 0) {
                rc = edit_app_doc_reread(doc, change, &err);
            }
            if (rc != 0) {
                edit_app_error(app, ctx, err);
            }
        } else {
            edit_tbuf_set_encoding(&doc->buffer->tbuf, change);
        }
        app->wants_encoding_change = EDIT_APP_ENC_NONE;
        edit_ctx_needs_rerender(ctx);
    }
}

// Moves doc to front (cf. Rust update_active with a widget closure).
static void docs_move_to_front(edit_app_t *app, edit_app_doc_t *doc) {
    if (app == NULL || doc == NULL || app->documents.first == doc) {
        return;
    }
    if (doc->prev != NULL) {
        doc->prev->next = doc->next;
    }
    if (doc->next != NULL) {
        doc->next->prev = doc->prev;
    }
    doc->prev = NULL;
    doc->next = app->documents.first;
    if (app->documents.first != NULL) {
        app->documents.first->prev = doc;
    }
    app->documents.first = doc;
}

static void draw_document_picker(edit_ctx_t *ctx, edit_app_t *app) {
    edit_ctx_modal_begin(ctx, "document-picker", "");
    edit_size_t size = edit_tui_size(ctx->tui);
    int32_t width = size.width - 20;
    int32_t height = size.height - 10;
    if (width < 10) {
        width = 10;
    }
    if (height < 10) {
        height = 10;
    }
    edit_size_t isz = {width, height};
    edit_ctx_scrollarea_begin(ctx, "scrollarea", isz);
    edit_ctx_attr_bg(ctx, edit_tui_indexed_alpha(ctx->tui, EDIT_FB_BLACK, 1, 4));
    edit_ctx_inherit_focus(ctx);
    edit_ctx_list_begin(ctx, "documents");
    edit_ctx_inherit_focus(ctx);
    edit_app_doc_t *chosen = NULL;
    for (edit_app_doc_t *doc = app->documents.first; doc != NULL; doc = doc->next) {
        edit_tbuf_t *tb = &doc->buffer->tbuf;
        edit_ctx_styled_list_item_begin(ctx);
        edit_ctx_set_overflow(ctx, 3);
        edit_ctx_styled_add(ctx, edit_tbuf_is_dirty(tb) ? "* " : "  ", 2);
        const char *name = doc->filename != NULL ? doc->filename : "";
        edit_ctx_styled_add(ctx, name, strlen(name));
        if (doc->dir != NULL) {
            edit_ctx_styled_add(ctx, "   ", 3);
            edit_ctx_styled_attr(ctx, EDIT_FB_ATTR_ITALIC);
            edit_ctx_styled_add(ctx, doc->dir, strlen(doc->dir));
        }
        if (edit_ctx_styled_list_item_end(ctx, false) == 2) {
            chosen = doc;
        }
    }
    edit_ctx_list_end(ctx);
    edit_ctx_scrollarea_end(ctx);
    if (edit_ctx_modal_end(ctx)) {
        app->wants_document_picker = false;
    }
    if (chosen != NULL) {
        docs_move_to_front(app, chosen);
        app->wants_document_picker = false;
        edit_ctx_needs_rerender(ctx);
    }
}

// Returns true if path has a parent dir (cf. Rust Path::parent().is_some()).
static bool path_has_parent(const char *path) {
    if (path == NULL || path[0] == '\0') {
        return false;
    }
    // "/" and "" have no parent; everything else does.
    return strcmp(path, "/") != 0;
}

static int entry_cmp(const void *a, const void *b) {
    const char *sa = *(const char *const *)a;
    const char *sb = *(const char *const *)b;
    size_t na = strlen(sa);
    size_t nb = strlen(sb);
    bool da = na > 0 && sa[na - 1] == '/';
    bool db = nb > 0 && sb[nb - 1] == '/';
    // Directories first (Rust: b_is_dir.cmp(a_is_dir) = descending).
    if (da != db) {
        return da ? -1 : 1;
    }
    return edit_compare_strings((const uint8_t *)sa, na, (const uint8_t *)sb, nb);
}

static void picker_entries_free(edit_app_t *app) {
    if (app->picker_entries != NULL) {
        for (size_t i = 0; i < app->picker_nentries; ++i) {
            free(app->picker_entries[i]);
        }
        free(app->picker_entries);
        app->picker_entries = NULL;
        app->picker_nentries = 0;
    }
}

static void draw_saveas_refresh_files(edit_app_t *app) {
    picker_entries_free(app);
    const char *dir = app->picker_dir != NULL ? app->picker_dir : "";
    size_t cap = 16;
    char **files = (char **)malloc(cap * sizeof *files);
    size_t n = 0;
    if (files == NULL) {
        return;
    }
    if (path_has_parent(dir)) {
        files[n++] = dup_str("..");
    }
    DIR *dp = opendir(dir);
    if (dp != NULL) {
        struct dirent *de = NULL;
        while ((de = readdir(dp)) != NULL) {
            if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
                continue;
            }
            // stat the full path (follows symlinks like Rust fs::metadata).
            char full[4096];
            int m = snprintf(full, sizeof full, "%s/%s", dir, de->d_name);
            if (m < 0 || (size_t)m >= sizeof full) {
                continue;
            }
            struct stat st;
            if (stat(full, &st) != 0) {
                continue;
            }
            bool is_dir = S_ISDIR(st.st_mode);
            size_t nl = strlen(de->d_name);
            char *name = (char *)malloc(nl + (is_dir ? 2 : 1));
            if (name == NULL) {
                continue;
            }
            memcpy(name, de->d_name, nl);
            if (is_dir) {
                name[nl] = '/';
                name[nl + 1] = '\0';
            } else {
                name[nl] = '\0';
            }
            if (n == cap) {
                size_t grown = cap * 2;
                char **nb = (char **)realloc(files, grown * sizeof *files);
                if (nb == NULL) {
                    free(name);
                    break;
                }
                files = nb;
                cap = grown;
            }
            files[n++] = name;
        }
        closedir(dp);
    }
    // Sort everything after ".." (Rust sorts files[off..] where off = len-1).
    if (n > 1) {
        size_t off = n - 1;
        if (files[0] != NULL && strcmp(files[0], "..") == 0) {
            qsort(files + 1, n - 1, sizeof *files, entry_cmp);
        } else {
            qsort(files + off, n - off, sizeof *files, entry_cmp);
        }
    }
    app->picker_entries = files;
    app->picker_nentries = n;
}

// Returns a heap path when the pending selection names a file, else NULL.
// Updates pending dir/name like Rust draw_file_picker_update_path.
static char *draw_file_picker_update_path(edit_app_t *app) {
    const char *dir = app->picker_dir != NULL ? app->picker_dir : "";
    const char *name = app->picker_name != NULL ? app->picker_name : "";
    size_t dl = strlen(dir);
    size_t nl = strlen(name);
    size_t jl = dl + 1 + nl + 1;
    char *joined = (char *)malloc(jl);
    if (joined == NULL) {
        return NULL;
    }
    memcpy(joined, dir, dl);
    joined[dl] = '/';
    memcpy(joined + dl + 1, name, nl + 1);
    size_t need = edit_path_normalize(NULL, 0, joined);
    char *norm = (char *)malloc(need + 1);
    if (norm == NULL) {
        free(joined);
        return NULL;
    }
    edit_path_normalize(norm, need + 1, joined);
    free(joined);

    struct stat st;
    char *new_dir = NULL;
    char *new_name = NULL;
    char *result = NULL;
    if (stat(norm, &st) == 0 && S_ISDIR(st.st_mode)) {
        new_dir = norm;
        new_name = dup_str("");
    } else {
        const char *slash = strrchr(norm, '/');
        if (slash != NULL) {
            size_t dn = (size_t)(slash - norm);
            if (dn == 0) {
                dn = 1;
            }
            new_dir = (char *)malloc(dn + 1);
            if (new_dir != NULL) {
                memcpy(new_dir, norm, dn);
                new_dir[dn] = '\0';
            }
            new_name = dup_str(slash + 1);
        } else {
            new_dir = dup_str("");
            new_name = dup_str(norm);
        }
        result = norm;
        norm = NULL;
    }
    free(norm);
    if (new_dir == NULL || new_name == NULL) {
        free(new_dir);
        free(new_name);
        free(result);
        return NULL;
    }
    if (app->picker_dir == NULL || strcmp(new_dir, app->picker_dir) != 0) {
        free(app->picker_dir);
        app->picker_dir = new_dir;
        picker_entries_free(app);
    } else {
        free(new_dir);
    }
    free(app->picker_name);
    app->picker_name = new_name;
    if (new_name[0] == '\0') {
        free(result);
        return NULL;
    }
    return result;
}

static void draw_file_picker(edit_ctx_t *ctx, edit_app_t *app) {
    if (app->picker == EDIT_APP_PICKER_SAVE_AS) {
        app->picker = EDIT_APP_PICKER_SAVE_AS_SHOWN;
        if (app->picker_name == NULL || app->picker_name[0] == '\0') {
            edit_app_doc_t *doc = edit_app_active(app);
            const char *name =
                (doc != NULL && doc->filename != NULL) ? doc->filename : "Untitled.txt";
            free(app->picker_name);
            app->picker_name = dup_str(name);
        }
    }
    edit_size_t size = edit_tui_size(ctx->tui);
    int32_t width = size.width - 20;
    int32_t height = size.height - 10;
    if (width < 10) {
        width = 10;
    }
    if (height < 10) {
        height = 10;
    }
    char *doit = NULL;
    bool done = false;

    edit_ctx_modal_begin(ctx, "file-picker",
                         app->picker == EDIT_APP_PICKER_OPEN ? edit_loc(EDIT_LOC_FILE_OPEN)
                                                             : edit_loc(EDIT_LOC_FILE_SAVE_AS));
    edit_size_t msz = {width, height};
    edit_ctx_attr_intrinsic_size(ctx, msz);
    bool activated = false;
    edit_ctx_table_begin(ctx, "path");
    int32_t cols[2] = {0, EDIT_COORD_SAFE_MAX};
    edit_ctx_table_set_columns(ctx, cols, 2);
    edit_size_t pgap = {1, 0};
    edit_ctx_table_set_gap(ctx, pgap);
    edit_ctx_attr_padding(ctx, pad2(1, 1));
    edit_ctx_inherit_focus(ctx);
    edit_ctx_table_next_row(ctx);
    edit_ctx_label(ctx, "dir-label", edit_loc(EDIT_LOC_SAVE_AS_DIALOG_PATH_LABEL));
    edit_ctx_label(ctx, "dir", app->picker_dir != NULL ? app->picker_dir : "");
    edit_ctx_set_overflow(ctx, 2);
    edit_ctx_table_next_row(ctx);
    edit_ctx_inherit_focus(ctx);
    edit_ctx_label(ctx, "name-label", edit_loc(EDIT_LOC_SAVE_AS_DIALOG_NAME_LABEL));
    // editline needs heap bufs; ensure non-NULL.
    if (app->picker_name == NULL) {
        app->picker_name = dup_str("");
    }
    size_t name_len = app->picker_name != NULL ? strlen(app->picker_name) : 0;
    size_t name_cap = name_len + 1;
    if (app->picker_name != NULL) {
        char *tmp = app->picker_name;
        size_t tmp_cap = name_cap;
        edit_ctx_editline_str(ctx, "name", &tmp, &name_len, &tmp_cap);
        app->picker_name = tmp;
    }
    edit_ctx_inherit_focus(ctx);
    if (edit_ctx_is_focused(ctx) && edit_ctx_consume_shortcut(ctx, EDIT_VK_RETURN)) {
        activated = true;
    }
    edit_ctx_table_end(ctx);

    if (app->picker_entries == NULL) {
        draw_saveas_refresh_files(app);
    }
    edit_size_t ssz = {0, height - 3};
    edit_ctx_scrollarea_begin(ctx, "directory", ssz);
    edit_ctx_attr_bg(ctx, edit_tui_indexed_alpha(ctx->tui, EDIT_FB_BLACK, 1, 4));
    edit_ctx_id_mixin(ctx, (uint64_t)(app->picker_dir != NULL ? strlen(app->picker_dir) : 0));
    edit_ctx_list_begin(ctx, "files");
    edit_ctx_inherit_focus(ctx);
    for (size_t i = 0; i < app->picker_nentries; ++i) {
        const char *entry = app->picker_entries[i];
        bool selected = app->picker_name != NULL && strcmp(app->picker_name, entry) == 0;
        int sel = edit_ctx_list_item(ctx, selected, entry);
        if (sel == 1) {
            free(app->picker_name);
            app->picker_name = dup_str(entry);
        } else if (sel == 2) {
            activated = true;
        }
        edit_ctx_set_overflow(ctx, 2);
    }
    edit_ctx_list_end(ctx);
    if (edit_ctx_contains_focus(ctx) && edit_ctx_consume_shortcut(ctx, EDIT_VK_BACK)) {
        free(app->picker_name);
        app->picker_name = dup_str("..");
        activated = true;
    }
    edit_ctx_scrollarea_end(ctx);

    if (activated) {
        doit = draw_file_picker_update_path(app);
        if (app->picker != EDIT_APP_PICKER_OPEN && doit != NULL) {
            edit_app_doc_t *doc = edit_app_active(app);
            if (doc != NULL && doc->has_file_id) {
                uint64_t dev = 0, ino = 0;
                if (edit_tty_file_id_at(doit, &dev, &ino) && dev == doc->file_dev &&
                    ino == doc->file_ino) {
                    free(app->picker_overwrite);
                    app->picker_overwrite = doit;
                    doit = NULL;
                }
            }
        }
    }
    if (edit_ctx_modal_end(ctx)) {
        done = true;
    }

    if (app->picker_overwrite != NULL) {
        bool save = false;
        edit_ctx_modal_begin(ctx, "overwrite", edit_loc(EDIT_LOC_FILE_OVERWRITE_WARNING));
        edit_ctx_attr_bg(ctx, edit_tui_indexed(ctx->tui, EDIT_FB_RED));
        edit_ctx_attr_fg(ctx, edit_tui_indexed(ctx->tui, EDIT_FB_BRIGHT_WHITE));
        edit_ctx_label(ctx, "description", edit_loc(EDIT_LOC_FILE_OVERWRITE_WARNING_DESCRIPTION));
        edit_ctx_set_overflow(ctx, 3);
        edit_ctx_attr_padding(ctx, pad3(1, 2, 1));
        edit_ctx_table_begin(ctx, "choices");
        edit_ctx_inherit_focus(ctx);
        edit_ctx_attr_padding(ctx, pad3(0, 2, 1));
        edit_ctx_attr_position(ctx, EDIT_POS_CENTER);
        edit_size_t cgap = {2, 0};
        edit_ctx_table_set_gap(ctx, cgap);
        edit_ctx_table_next_row(ctx);
        edit_ctx_inherit_focus(ctx);
        save = edit_ctx_button(ctx, "yes", edit_loc(EDIT_LOC_YES));
        edit_ctx_inherit_focus(ctx);
        if (edit_ctx_button(ctx, "no", edit_loc(EDIT_LOC_NO))) {
            free(app->picker_overwrite);
            app->picker_overwrite = NULL;
        }
        edit_ctx_table_end(ctx);
        save |= edit_ctx_consume_shortcut(ctx, (uint32_t)'Y');
        if (edit_ctx_consume_shortcut(ctx, (uint32_t)'N')) {
            free(app->picker_overwrite);
            app->picker_overwrite = NULL;
        }
        if (edit_ctx_modal_end(ctx)) {
            free(app->picker_overwrite);
            app->picker_overwrite = NULL;
        }
        if (save) {
            free(doit);
            doit = app->picker_overwrite;
            app->picker_overwrite = NULL;
        }
    }

    if (doit != NULL) {
        edit_error_t err = edit_error_app(0);
        int rc = 0;
        if (app->picker == EDIT_APP_PICKER_OPEN) {
            rc = edit_app_add_file_path(app, doit);
            // add_file_path drops errno; map failures generically below.
            if (rc != 0) {
                err = edit_error_sys((uint32_t)errno);
            }
        } else {
            edit_app_doc_t *doc = edit_app_active(app);
            if (doc != NULL) {
                rc = edit_app_doc_save(app, doc, doit, &err);
            }
        }
        if (rc == 0) {
            edit_ctx_needs_rerender(ctx);
            done = true;
        } else {
            edit_app_error(app, ctx, err);
        }
        free(doit);
    }

    if (done) {
        app->picker = EDIT_APP_PICKER_NONE;
        free(app->picker_name);
        app->picker_name = NULL;
        picker_entries_free(app);
        free(app->picker_overwrite);
        app->picker_overwrite = NULL;
    }
}

static void draw_handle_wants_exit(edit_ctx_t *ctx, edit_app_t *app) {
    (void)ctx;
    for (;;) {
        edit_app_doc_t *doc = edit_app_active(app);
        if (doc == NULL) {
            break;
        }
        if (edit_tbuf_is_dirty(&doc->buffer->tbuf)) {
            app->wants_close = true;
            return;
        }
        edit_app_remove_active(app);
    }
    if (app->documents.len == 0) {
        app->exit = true;
    }
}

static void draw_handle_clipboard_change(edit_ctx_t *ctx, edit_app_t *app) {
    uint32_t generation = edit_ctx_clipboard_gen(ctx);
    const uint8_t *clip = NULL;
    size_t clip_len = edit_ctx_clipboard(ctx, &clip);
    if (app->osc_clip_always || clip_len < 4 * 1024) {
        app->osc_clip_seen = generation;
        app->osc_clip_send = generation;
        return;
    }
    // Over the scratch limit? (512MiB/4 cap like Rust SCRATCH/4).
    bool over_limit = clip_len >= (size_t)128 * 1024 * 1024;
    edit_ctx_modal_begin(ctx, "warning", edit_loc(EDIT_LOC_WARNING_DIALOG_TITLE));
    edit_ctx_block_begin(ctx, "description");
    edit_ctx_attr_padding(ctx, pad3(1, 2, 1));
    if (over_limit) {
        edit_ctx_label(ctx, "line1", edit_loc(EDIT_LOC_LARGE_CLIPBOARD_WARNING_LINE1));
        edit_ctx_attr_position(ctx, EDIT_POS_CENTER);
        edit_ctx_label(ctx, "line2", edit_loc(EDIT_LOC_SUPER_LARGE_CLIPBOARD_WARNING));
        edit_ctx_attr_position(ctx, EDIT_POS_CENTER);
    } else {
        char size[64];
        edit_metric_format(size, sizeof size, clip_len);
        const char *templ = edit_loc(EDIT_LOC_LARGE_CLIPBOARD_WARNING_LINE2);
        const char *ph = strstr(templ, "{size}");
        char line2[512];
        if (ph != NULL) {
            size_t pre = (size_t)(ph - templ);
            snprintf(line2, sizeof line2, "%.*s%s%s", (int)pre, templ, size, ph + 6);
        } else {
            snprintf(line2, sizeof line2, "%s", templ);
        }
        edit_ctx_label(ctx, "line1", edit_loc(EDIT_LOC_LARGE_CLIPBOARD_WARNING_LINE1));
        edit_ctx_attr_position(ctx, EDIT_POS_CENTER);
        edit_ctx_label(ctx, "line2", line2);
        edit_ctx_attr_position(ctx, EDIT_POS_CENTER);
        edit_ctx_label(ctx, "line3", edit_loc(EDIT_LOC_LARGE_CLIPBOARD_WARNING_LINE3));
        edit_ctx_attr_position(ctx, EDIT_POS_CENTER);
    }
    edit_ctx_block_end(ctx);
    edit_ctx_table_begin(ctx, "choices");
    edit_ctx_inherit_focus(ctx);
    edit_ctx_attr_padding(ctx, pad3(0, 2, 1));
    edit_ctx_attr_position(ctx, EDIT_POS_CENTER);
    edit_size_t cgap = {2, 0};
    edit_ctx_table_set_gap(ctx, cgap);
    edit_ctx_table_next_row(ctx);
    edit_ctx_inherit_focus(ctx);
    if (over_limit) {
        if (edit_ctx_button(ctx, "ok", edit_loc(EDIT_LOC_OK))) {
            app->osc_clip_seen = generation;
        }
        edit_ctx_inherit_focus(ctx);
    } else {
        if (edit_ctx_button(ctx, "always", edit_loc(EDIT_LOC_ALWAYS))) {
            app->osc_clip_always = true;
            app->osc_clip_seen = generation;
            app->osc_clip_send = generation;
        }
        if (edit_ctx_button(ctx, "yes", edit_loc(EDIT_LOC_YES))) {
            app->osc_clip_seen = generation;
            app->osc_clip_send = generation;
        }
        if (clip_len < 10 * 4 * 1024) {
            edit_ctx_inherit_focus(ctx);
        }
        if (edit_ctx_button(ctx, "no", edit_loc(EDIT_LOC_NO))) {
            app->osc_clip_seen = generation;
        }
        if (clip_len >= 10 * 4 * 1024) {
            edit_ctx_inherit_focus(ctx);
        }
    }
    edit_ctx_table_end(ctx);
    if (edit_ctx_modal_end(ctx)) {
        app->osc_clip_seen = generation;
    }
}

void edit_app_draw(edit_ctx_t *ctx, edit_app_t *app) {
    if (ctx == NULL || app == NULL) {
        return;
    }
    edit_app_draw_menubar(ctx, app);
    draw_editor(ctx, app);
    draw_statusbar(ctx, app);

    if (app->wants_close) {
        draw_handle_wants_close(ctx, app);
    }
    if (app->wants_exit) {
        draw_handle_wants_exit(ctx, app);
    }
    if (app->picker != EDIT_APP_PICKER_NONE) {
        draw_file_picker(ctx, app);
    }
    if (app->wants_save) {
        draw_handle_save(ctx, app);
    }
    if (app->wants_encoding_change != EDIT_APP_ENC_NONE) {
        draw_dialog_encoding_change(ctx, app);
    }
    if (app->wants_document_picker) {
        draw_document_picker(ctx, app);
    }
    if (app->wants_about) {
        draw_dialog_about(ctx, app);
    }
    if (app->osc_clip_seen != edit_ctx_clipboard_gen(ctx)) {
        draw_handle_clipboard_change(ctx, app);
    }
    if (app->error_count != 0) {
        draw_error_log(ctx, app);
    }

    uint32_t key = 0;
    if (edit_ctx_keyboard_input(ctx, &key)) {
        bool handled = true;
        if (key == ((uint32_t)EDIT_KBMOD_CTRL | (uint32_t)'N')) {
            draw_add_untitled(ctx, app);
        } else if (key == ((uint32_t)EDIT_KBMOD_CTRL | (uint32_t)'O')) {
            app->picker = EDIT_APP_PICKER_OPEN;
        } else if (key == ((uint32_t)EDIT_KBMOD_CTRL | (uint32_t)'S')) {
            app->wants_save = true;
        } else if (key ==
                   ((uint32_t)EDIT_KBMOD_CTRL | (uint32_t)EDIT_KBMOD_SHIFT | (uint32_t)'S')) {
            app->picker = EDIT_APP_PICKER_SAVE_AS;
        } else if (key == ((uint32_t)EDIT_KBMOD_CTRL | (uint32_t)'W')) {
            app->wants_close = true;
        } else if (key == ((uint32_t)EDIT_KBMOD_CTRL | (uint32_t)'P')) {
            app->wants_document_picker = true;
        } else if (key == ((uint32_t)EDIT_KBMOD_CTRL | (uint32_t)'Q')) {
            app->wants_exit = true;
        } else if (key == ((uint32_t)EDIT_KBMOD_CTRL | (uint32_t)'F') &&
                   app->search_kind != EDIT_APP_SEARCH_DISABLED) {
            app->search_kind = EDIT_APP_SEARCH;
            app->search_focus = true;
        } else if (key == ((uint32_t)EDIT_KBMOD_CTRL | (uint32_t)'R') &&
                   app->search_kind != EDIT_APP_SEARCH_DISABLED) {
            app->search_kind = EDIT_APP_SEARCH_REPLACE;
            app->search_focus = true;
        } else {
            handled = false;
        }
        if (handled) {
            edit_ctx_needs_rerender(ctx);
            edit_ctx_set_consumed(ctx);
        }
    }
}

size_t edit_app_help_text(char *dst, size_t cap) {
    const char *text = "Usage: edit [OPTIONS] [FILE]\r\n"
                       "Options:\r\n"
                       "    -h, --help       Print this help message\r\n"
                       "    -v, --version    Print the version number\r\n";
    size_t n = strlen(text);
    if (dst != NULL && cap > 0) {
        size_t k = n < cap ? n : cap - 1;
        memcpy(dst, text, k);
        dst[k] = '\0';
    }
    return n;
}

size_t edit_app_version_text(char *dst, size_t cap) {
    const char *prefix = "edit version ";
    size_t n = strlen(prefix) + strlen(EDIT_APP_VERSION) + 2;
    if (dst != NULL && cap > 0) {
        snprintf(dst, cap, "%s%s\r\n", prefix, EDIT_APP_VERSION);
    }
    return n;
}

// Strips C0 controls to '_' (cf. Rust sanitize_control_chars).
static void sanitize_title(char *dst, size_t cap, const char *filename) {
    if (dst == NULL || cap == 0) {
        return;
    }
    size_t n = strlen(filename);
    if (n > cap - 1) {
        n = cap - 1;
    }
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)filename[i];
        dst[i] = (char)(c < 0x20 ? '_' : c);
    }
    dst[n] = '\0';
}

size_t edit_app_title_seq(char *dst, size_t cap, const char *filename) {
    char clean[1024];
    sanitize_title(clean, sizeof clean, filename != NULL ? filename : "");
    int n;
    if (clean[0] != '\0') {
        n = snprintf(dst, cap, "\x1b]0;%s - edit\x1b\\", clean);
    } else {
        n = snprintf(dst, cap, "\x1b]0;edit\x1b\\");
    }
    if (n < 0) {
        if (dst != NULL && cap > 0) {
            dst[0] = '\0';
        }
        return 0;
    }
    return (size_t)n;
}

size_t edit_app_clipboard_seq(char *dst, size_t cap, const uint8_t *clip, size_t clip_len) {
    if (clip == NULL || clip_len == 0) {
        if (dst != NULL && cap > 0) {
            dst[0] = '\0';
        }
        return 0;
    }
    size_t b64 = 0;
    if (!edit_base64_encode_len(clip_len, &b64)) {
        if (dst != NULL && cap > 0) {
            dst[0] = '\0';
        }
        return 0;
    }
    // "\x1b]52;c;" + b64 + "\x1b\\". Callers size dst with need + 1;
    // smaller buffers get a truncated (still NUL-terminated) prefix.
    size_t need = 7 + b64 + 2;
    if (dst != NULL && cap > 0) {
        size_t pos = 0;
        const char *pre = "\x1b]52;c;";
        size_t k = 7 < cap - 1 ? 7 : cap - 1;
        memcpy(dst, pre, k);
        pos = k;
        if (pos + 2 < cap && cap - pos - 2 >= b64 && b64 > 0) {
            if (edit_base64_encode(dst + pos, cap - pos, clip, clip_len) == 0) {
                pos += b64;
            }
        }
        if (pos + 2 <= cap) {
            dst[pos++] = '\x1b';
            dst[pos++] = '\\';
        }
        dst[pos < cap ? pos : cap - 1] = '\0';
    }
    return need;
}

int edit_app_handle_args(edit_app_t *app, int argc, char **argv, bool *out_early_exit,
                         edit_error_t *err) {
    if (out_early_exit != NULL) {
        *out_early_exit = false;
    }
    if (err != NULL) {
        *err = edit_error_app(0);
    }
    if (app == NULL || argv == NULL) {
        return -1;
    }
    char cwd[4096];
    if (getcwd(cwd, sizeof cwd) == NULL) {
        if (err != NULL) {
            *err = edit_error_sys((uint32_t)errno);
        }
        return -1;
    }
    const char *arg = argc > 1 ? argv[1] : NULL;
    const char *path = NULL;
    char *owned_path = NULL;
    if (arg != NULL) {
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            char buf[256];
            edit_app_help_text(buf, sizeof buf);
            edit_tty_write((const uint8_t *)buf, strlen(buf));
            if (out_early_exit != NULL) {
                *out_early_exit = true;
            }
            return 0;
        }
        if (strcmp(arg, "-v") == 0 || strcmp(arg, "--version") == 0) {
            char buf[64];
            edit_app_version_text(buf, sizeof buf);
            edit_tty_write((const uint8_t *)buf, strlen(buf));
            if (out_early_exit != NULL) {
                *out_early_exit = true;
            }
            return 0;
        }
        if (strcmp(arg, "-") != 0) {
            // cwd.join(arg) then normalize (cf. Rust handle_args).
            size_t al = strlen(arg);
            size_t cl = strlen(cwd);
            char *joined = (char *)malloc(cl + 1 + al + 1);
            if (joined == NULL) {
                return -1;
            }
            memcpy(joined, cwd, cl);
            joined[cl] = '/';
            memcpy(joined + cl + 1, arg, al + 1);
            size_t need = edit_path_normalize(NULL, 0, joined);
            owned_path = (char *)malloc(need + 1);
            if (owned_path == NULL) {
                free(joined);
                return -1;
            }
            edit_path_normalize(owned_path, need + 1, joined);
            free(joined);
            path = owned_path;
            // Parent becomes the picker dir.
            const char *slash = strrchr(owned_path, '/');
            size_t dn = 0;
            if (slash != NULL) {
                dn = (size_t)(slash - owned_path);
                if (dn == 0) {
                    dn = 1;
                }
            }
            char *dir = (char *)malloc(dn + 1);
            if (dir == NULL) {
                free(owned_path);
                return -1;
            }
            memcpy(dir, owned_path, dn);
            dir[dn] = '\0';
            free(app->picker_dir);
            app->picker_dir = dir;
        }
    }

    int rc = 0;
    if (edit_tty_stdin_redirected()) {
        if (edit_app_add_untitled(app) != 0) {
            rc = -1;
        } else {
            edit_app_doc_t *doc = edit_app_active(app);
            edit_error_t rerr = edit_error_app(0);
            if (doc == NULL ||
                edit_tbuf_read_file(&doc->buffer->tbuf, STDIN_FILENO, NULL, &rerr) != 0) {
                if (err != NULL) {
                    *err = rerr;
                }
                rc = -1;
            } else {
                edit_tbuf_mark_dirty(&doc->buffer->tbuf);
            }
        }
    } else if (path != NULL) {
        if (edit_app_add_file_path(app, path) != 0) {
            if (err != NULL) {
                *err = edit_error_sys((uint32_t)errno);
            }
            rc = -1;
        }
    } else {
        if (edit_app_add_untitled(app) != 0) {
            rc = -1;
        }
    }
    if (rc == 0 && app->picker_dir == NULL) {
        app->picker_dir = dup_str(cwd);
        if (app->picker_dir == NULL) {
            rc = -1;
        }
    }
    free(owned_path);
    return rc;
}
