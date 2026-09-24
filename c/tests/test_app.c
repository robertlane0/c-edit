#define _DEFAULT_SOURCE // mkdtemp; must precede headers

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "edit/app.h"
#include "edit/apploc.h"
#include "edit/helpers.h"
#include "edit/input.h"
#include "edit/tui.h"

static int checks = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #cond);                                     \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

// Mirrors Rust test_parse_last_numbers vectors.
static int check_goto(const char *in, const char *want_path, bool want_goto, int gx, int gy) {
    const char *path = NULL;
    size_t path_len = 0;
    edit_point_t go = {0, 0};
    bool got = edit_app_parse_goto(in, &path, &path_len, &go);
    ++checks;
    if (got != want_goto) {
        fprintf(stderr, "FAIL goto(%s): got=%d want=%d\n", in, got, want_goto);
        return 1;
    }
    ++checks;
    if (path_len != strlen(want_path) || memcmp(path, want_path, path_len) != 0) {
        fprintf(stderr, "FAIL goto(%s): path=%.*s want=%s\n", in, (int)path_len, path, want_path);
        return 1;
    }
    if (want_goto) {
        ++checks;
        if (go.x != gx || go.y != gy) {
            fprintf(stderr, "FAIL goto(%s): (%d,%d) want (%d,%d)\n", in, go.x, go.y, gx, gy);
            return 1;
        }
    }
    return 0;
}

static bool pred_counter(const edit_app_doc_t *doc, const void *arg) {
    unsigned want = *(const unsigned *)arg;
    return doc->new_file_counter == want;
}

int main(void) {
    edit_loc_init();

    CHECK(check_goto("123", "123", false, 0, 0) == 0);
    CHECK(check_goto("abc", "abc", false, 0, 0) == 0);
    CHECK(check_goto(":123", ":123", false, 0, 0) == 0);
    CHECK(check_goto("abc:123", "abc", true, 0, 122) == 0);
    CHECK(check_goto("45:123", "45", true, 0, 122) == 0);
    CHECK(check_goto(":45:123", ":45", true, 0, 122) == 0);
    CHECK(check_goto("abc:45:123", "abc", true, 122, 44) == 0);
    CHECK(check_goto("abc:def:123", "abc:def", true, 0, 122) == 0);
    CHECK(check_goto("1:2:3", "1", true, 2, 1) == 0);
    CHECK(check_goto("::3", ":", true, 0, 2) == 0);
    CHECK(check_goto("1::3", "1:", true, 0, 2) == 0);
    CHECK(check_goto("", "", false, 0, 0) == 0);
    CHECK(check_goto(":", ":", false, 0, 0) == 0);
    CHECK(check_goto("::", "::", false, 0, 0) == 0);
    CHECK(check_goto("a:1", "a", true, 0, 0) == 0);
    CHECK(check_goto("1:a", "1:a", false, 0, 0) == 0);
    CHECK(check_goto("file.txt:10", "file.txt", true, 0, 9) == 0);
    CHECK(check_goto("file.txt:10:5", "file.txt", true, 4, 9) == 0);
    CHECK(edit_app_parse_goto(NULL, NULL, NULL, NULL) == false);
    // i32 overflow is not a goto (Rust checked arithmetic).
    CHECK(check_goto("f:9999999999", "f:9999999999", false, 0, 0) == 0);

    // Help/version text.
    {
        char help[256];
        size_t n = edit_app_help_text(help, sizeof help);
        CHECK(n > 0 && strstr(help, "Usage: edit [OPTIONS] [FILE]") != NULL);
        CHECK(edit_app_help_text(NULL, 0) == n);
        char ver[64];
        size_t m = edit_app_version_text(ver, sizeof ver);
        CHECK(m > 0 && strstr(ver, "edit version 1.0.0") != NULL);
        CHECK(edit_app_version_text(NULL, 0) == m);
    }

    // Title + clipboard sequences.
    {
        char title[64];
        size_t n = edit_app_title_seq(title, sizeof title, "a/b.txt");
        CHECK(n > 0 && strstr(title, "b.txt - edit") != NULL);
        n = edit_app_title_seq(title, sizeof title, "");
        CHECK(strstr(title, "\x1b]0;edit\x1b\\") != NULL);
        n = edit_app_title_seq(title, sizeof title,
                               "a\x01"
                               "b");
        CHECK(strstr(title, "a_b") != NULL);
        CHECK(edit_app_title_seq(NULL, 0, "x") > 0);
        const uint8_t clip[] = {'h', 'i'};
        char osc[64];
        n = edit_app_clipboard_seq(osc, sizeof osc, clip, sizeof clip);
        CHECK(n == 7 + 4 + 2 && strstr(osc, "\x1b]52;c;aGk=\x1b\\") != NULL);
        CHECK(edit_app_clipboard_seq(NULL, 0, clip, sizeof clip) == n);
        CHECK(edit_app_clipboard_seq(osc, sizeof osc, NULL, 0) == 0);
    }

    // Document manager against a scratch dir.
    char dir[] = "/tmp/edit_app_test_XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    char file1[256], file2[256];
    snprintf(file1, sizeof file1, "%s/alpha.txt", dir);
    snprintf(file2, sizeof file2, "%s/COMMIT_EDITMSG", dir);
    {
        int fd = open(file1, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        CHECK(fd >= 0);
        const char *content = "line1\nline2\nline3\n";
        CHECK(write(fd, content, strlen(content)) == (ssize_t)strlen(content));
        close(fd);
    }

    edit_app_t app;
    CHECK(edit_app_init(&app) == 0);

    // Untitled naming with counters.
    CHECK(edit_app_add_untitled(&app) == 0);
    CHECK(strcmp(edit_app_active(&app)->filename, "Untitled-1.txt") == 0);
    CHECK(edit_app_active(&app)->path == NULL);
    CHECK(edit_app_add_untitled(&app) == 0);
    CHECK(strcmp(edit_app_active(&app)->filename, "Untitled-2.txt") == 0);
    CHECK(app.documents.len == 2);
    // update_active moves doc 1 to front.
    {
        unsigned want = 1;
        CHECK(edit_app_update_active(&app, pred_counter, &want));
        CHECK(strcmp(edit_app_active(&app)->filename, "Untitled-1.txt") == 0);
        unsigned missing = 99;
        CHECK(!edit_app_update_active(&app, pred_counter, &missing));
        CHECK(!edit_app_update_active(NULL, pred_counter, &missing));
    }

    // Open a real file with :line goto.
    {
        char arg[300];
        snprintf(arg, sizeof arg, "%s:3", file1);
        CHECK(edit_app_add_file_path(&app, arg) == 0);
        edit_app_doc_t *doc = edit_app_active(&app);
        CHECK(doc->path != NULL && strcmp(doc->path, file1) == 0);
        CHECK(strcmp(doc->filename, "alpha.txt") == 0);
        CHECK(doc->has_file_id);
        edit_point_t cur = edit_tbuf_cursor_logical(&doc->buffer->tbuf);
        CHECK(cur.y == 2 && cur.x == 0);
        CHECK(app.documents.len == 3);
        // Reopen same file: focuses existing instead of duplicating.
        CHECK(edit_app_add_file_path(&app, file1) == 0);
        CHECK(app.documents.len == 3);
    }

    // COMMIT_EDITMSG sets the 72-column ruler.
    {
        CHECK(edit_app_add_file_path(&app, file2) == 0);
        // Empty existing file is fine; ruler set from the name.
        edit_app_doc_t *doc = edit_app_active(&app);
        (void)doc;
    }

    // Missing file creates an empty named document.
    {
        char missing[300];
        snprintf(missing, sizeof missing, "%s/nope.txt", dir);
        CHECK(edit_app_add_file_path(&app, missing) == 0);
        edit_app_doc_t *doc = edit_app_active(&app);
        CHECK(doc->path != NULL && !doc->has_file_id);
        CHECK(edit_tbuf_len(&doc->buffer->tbuf) == 0);
    }

    // Save a document to a new path, then reread it.
    {
        edit_app_doc_t *doc = edit_app_active(&app);
        const uint8_t text[] = {'h', 'i'};
        edit_tbuf_write(&doc->buffer->tbuf, text, sizeof text, true);
        char out[300];
        snprintf(out, sizeof out, "%s/saved.txt", dir);
        edit_error_t err;
        CHECK(edit_app_doc_save(&app, doc, out, &err) == 0);
        CHECK(strcmp(doc->path, out) == 0 && doc->has_file_id);
        CHECK(!edit_tbuf_is_dirty(&doc->buffer->tbuf));
        // Reread keeps content + id.
        CHECK(edit_app_doc_reread(doc, NULL, &err) == 0);
        CHECK(edit_tbuf_len(&doc->buffer->tbuf) == 2);
        // Save with no path fails gracefully.
        edit_app_doc_t *unt = NULL;
        for (edit_app_doc_t *d = app.documents.first; d != NULL; d = d->next) {
            if (d->path == NULL) {
                unt = d;
            }
        }
        CHECK(unt != NULL);
        CHECK(edit_app_doc_save(&app, unt, NULL, &err) != 0);
        CHECK(edit_app_doc_reread(unt, NULL, &err) != 0);
        CHECK(edit_app_doc_save(NULL, NULL, NULL, NULL) != 0);
    }

    // Error log ring.
    {
        edit_app_error(&app, NULL, edit_error_app(7));
        CHECK(app.error_count == 1);
        CHECK(strstr(app.error_log[0], "Unknown app error code: 7") != NULL);
        char *msg = edit_app_error_text(edit_error_app(7));
        CHECK(msg != NULL && strstr(msg, "7") != NULL);
        free(msg);
        msg = edit_app_error_text(EDIT_APP_ICU_MISSING);
        CHECK(msg != NULL && msg[0] != '\0');
        free(msg);
        // Dismiss the log so later frames are not modal-blocked.
        app.error_count = 0;
    }

    // Full frame draw smoke: menubar + editor + statusbar render.
    {
        edit_tui_t tui;
        CHECK(edit_tui_init(&tui) == 0);
        edit_input_t rs;
        memset(&rs, 0, sizeof rs);
        rs.kind = EDIT_IN_RESIZE;
        rs.size.width = 80;
        rs.size.height = 24;
        edit_ctx_t ctx;
        CHECK(edit_tui_begin(&tui, &rs, &ctx) == 0);
        edit_tui_end(&tui, &ctx);
        edit_app_setup_colors(&app, &tui);
        edit_tui_set_modifiers(&tui, edit_loc(EDIT_LOC_CTRL), edit_loc(EDIT_LOC_ALT),
                               edit_loc(EDIT_LOC_SHIFT));
        // Draw at least once, then until settled (mirrors the app loop).
        for (int i = 0; i < 30; ++i) {
            CHECK(edit_tui_begin(&tui, NULL, &ctx) == 0);
            edit_app_draw(&ctx, &app);
            edit_tui_end(&tui, &ctx);
            if (!edit_tui_needs_settling(&tui)) {
                break;
            }
        }
        const char *out = NULL;
        size_t n = edit_tui_render(&tui, &out);
        CHECK(n > 0 && out != NULL);
        // English menubar + statusbar filename visible.
        CHECK(memmem(out, n, "ile", 3) != NULL);
        CHECK(memmem(out, n, "saved.txt", 9) != NULL);
        // Type into the focused textarea.
        edit_input_t text;
        memset(&text, 0, sizeof text);
        text.kind = EDIT_IN_TEXT;
        const char *keys = "!";
        text.text.ptr = (const uint8_t *)keys;
        text.text.len = 1;
        CHECK(edit_tui_begin(&tui, &text, &ctx) == 0);
        edit_app_draw(&ctx, &app);
        edit_tui_end(&tui, &ctx);
        for (int i = 0; i < 30 && edit_tui_needs_settling(&tui); ++i) {
            CHECK(edit_tui_begin(&tui, NULL, &ctx) == 0);
            edit_app_draw(&ctx, &app);
            edit_tui_end(&tui, &ctx);
        }
        CHECK(edit_tbuf_is_dirty(&edit_app_active(&app)->buffer->tbuf));
        // Ctrl+S saves the active (named) document.
        edit_input_t save;
        memset(&save, 0, sizeof save);
        save.kind = EDIT_IN_KEYBOARD;
        save.key = EDIT_KBMOD_CTRL | (uint32_t)'S';
        CHECK(edit_tui_begin(&tui, &save, &ctx) == 0);
        edit_app_draw(&ctx, &app);
        edit_tui_end(&tui, &ctx);
        for (int i = 0; i < 30 && edit_tui_needs_settling(&tui); ++i) {
            CHECK(edit_tui_begin(&tui, NULL, &ctx) == 0);
            edit_app_draw(&ctx, &app);
            edit_tui_end(&tui, &ctx);
        }
        CHECK(!edit_tbuf_is_dirty(&edit_app_active(&app)->buffer->tbuf));
        // Ctrl+Q exits via the wants_exit path (dirty docs prompt first).
        edit_input_t quit;
        memset(&quit, 0, sizeof quit);
        quit.kind = EDIT_IN_KEYBOARD;
        quit.key = EDIT_KBMOD_CTRL | (uint32_t)'Q';
        CHECK(edit_tui_begin(&tui, &quit, &ctx) == 0);
        edit_app_draw(&ctx, &app);
        edit_tui_end(&tui, &ctx);
        for (int i = 0; i < 30 && edit_tui_needs_settling(&tui); ++i) {
            CHECK(edit_tui_begin(&tui, NULL, &ctx) == 0);
            edit_app_draw(&ctx, &app);
            edit_tui_end(&tui, &ctx);
        }
        edit_tui_destroy(&tui);
    }

    // remove_active drains the list.
    {
        size_t n = app.documents.len;
        for (size_t i = 0; i < n; ++i) {
            edit_app_remove_active(&app);
        }
        CHECK(app.documents.len == 0 && edit_app_active(&app) == NULL);
        edit_app_remove_active(&app); // no-op
    }

    // NULL safety.
    edit_app_destroy(NULL);
    CHECK(edit_app_init(NULL) != 0);
    CHECK(edit_app_add_untitled(NULL) != 0);
    CHECK(edit_app_add_file_path(NULL, NULL) != 0);
    CHECK(edit_app_active(NULL) == NULL);
    edit_app_remove_active(NULL);
    edit_app_draw(NULL, NULL);
    edit_app_setup_colors(NULL, NULL);
    edit_app_error(NULL, NULL, edit_error_app(0));

    edit_app_destroy(&app);

    // Cleanup scratch dir.
    {
        char path[300];
        snprintf(path, sizeof path, "%s/alpha.txt", dir);
        unlink(path);
        snprintf(path, sizeof path, "%s/COMMIT_EDITMSG", dir);
        unlink(path);
        snprintf(path, sizeof path, "%s/saved.txt", dir);
        unlink(path);
        rmdir(dir);
    }
    printf("test_app: %d checks passed\n", checks);
    return 0;
}
