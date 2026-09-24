#ifndef EDIT_TTY_H
#define EDIT_TTY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "edit/apperr.h"

#ifdef __cplusplus
extern "C" {
#endif

// Unix terminal core (cf. Rust sys::unix console parts). Single-threaded;
// global state mirrors Rust's STATE. Times in ms; -1 means wait forever.
#define EDIT_TTY_WAIT_FOREVER ((int64_t)(-1))

// Opens /dev/tty when stdin is redirected; records fd flags. 0 ok, -1 error.
int edit_tty_init(edit_error_t *err);
// Restores the saved terminal modes (argv of init/switch_modes).
void edit_tty_deinit(void);
// Raw mode + SIGWINCH tracking. 0 ok, -1 error.
int edit_tty_switch_modes(edit_error_t *err);
// Test/embedding hook: redirect the stdio fds used below.
void edit_tty_bind_fds(int stdin_fd, int stdout_fd);
// Flags the next read to prepend a window-size report.
void edit_tty_inject_resize(void);

typedef enum {
    EDIT_TTY_DATA,    // *out malloc'd (caller frees), *out_len > 0
    EDIT_TTY_TIMEOUT, // no input in time (*out NULL)
    EDIT_TTY_CLOSED,  // EOF or read error (*out NULL)
} edit_tty_read_t;

// Lossy-UTF8 stdin read with incomplete-sequence carryover.
edit_tty_read_t edit_tty_read(uint8_t **out, size_t *out_len, int64_t timeout_ms);
// Blocking raw write of the full buffer.
bool edit_tty_write(const uint8_t *text, size_t len);
// True if the last stdin was redirected (init opened /dev/tty).
bool edit_tty_stdin_redirected(void);
// fstat identity for a file descriptor.
bool edit_tty_file_id(int fd, uint64_t *out_dev, uint64_t *out_ino);
// Opens path read-only and reports its file id (cf. Rust file_id_at).
bool edit_tty_file_id_at(const char *path, uint64_t *out_dev, uint64_t *out_ino);
// First set env var among LANGUAGE/LC_ALL/LANG, split on ':'.
// Returns malloc'd list (*out_n entries, caller frees each + array).
size_t edit_tty_languages(char ***out);
// "Error N: strerror" formatting; returns needed length excl. NUL.
size_t edit_tty_error_text(uint32_t code, char *dst, size_t cap);
bool edit_tty_is_not_found(edit_error_t err);

#ifdef __cplusplus
}
#endif

#endif
