#ifndef EDIT_UTF8_H
#define EDIT_UTF8_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EDIT_UTF8_FFFD UINT32_C(0xFFFD)

// WHATWG-style decoder over unsanitized bytes; invalid sequences yield FFFD.
// Single-threaded, borrows src (must outlive the iterator).
typedef struct {
    const uint8_t *src;
    size_t len;
    size_t offset;
} edit_utf8_chars_t;

void edit_utf8_chars_init(edit_utf8_chars_t *it, const uint8_t *src, size_t len, size_t offset);
size_t edit_utf8_len(const edit_utf8_chars_t *it);
size_t edit_utf8_offset(const edit_utf8_chars_t *it);
bool edit_utf8_is_empty(const edit_utf8_chars_t *it);
bool edit_utf8_has_next(const edit_utf8_chars_t *it);
void edit_utf8_seek(edit_utf8_chars_t *it, size_t offset);
// Next codepoint (valid or FFFD); false when exhausted or on NULL args.
bool edit_utf8_next(edit_utf8_chars_t *it, uint32_t *out);

#ifdef __cplusplus
}
#endif

#endif
