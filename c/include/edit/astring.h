#ifndef EDIT_ASTRING_H
#define EDIT_ASTRING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "edit/avec.h"

#ifdef __cplusplus
extern "C" {
#endif

EDIT_AVEC_DEFINE(edit_av_u8, uint8_t)

// Arena-backed UTF-8 string (cf. Rust ArenaString). Always valid UTF-8;
// bytes enter only via validated paths. Bound to one arena for life.
typedef struct {
    edit_av_u8_t vec;
} edit_astring_t;

void edit_astring_init(edit_astring_t *s, edit_arena_t *arena);
bool edit_astring_with_capacity(edit_astring_t *s, edit_arena_t *arena, size_t capacity);
bool edit_astring_from_str(edit_astring_t *s, edit_arena_t *arena, const char *str, size_t len);
size_t edit_astring_len(const edit_astring_t *s);
size_t edit_astring_capacity(const edit_astring_t *s);
bool edit_astring_is_empty(const edit_astring_t *s);
const char *edit_astring_data(const edit_astring_t *s); // bytes, not NUL-terminated
bool edit_astring_reserve(edit_astring_t *s, size_t additional);
bool edit_astring_reserve_exact(edit_astring_t *s, size_t additional);
bool edit_astring_shrink_to_fit(edit_astring_t *s); // tail-only, like Rust
void edit_astring_clear(edit_astring_t *s);
bool edit_astring_push_str(edit_astring_t *s, const char *str, size_t len);
bool edit_astring_push(edit_astring_t *s, uint32_t cp);
bool edit_astring_push_repeat(edit_astring_t *s, uint32_t cp, size_t n);
// Byte range; boundaries asserted as char boundaries in debug.
bool edit_astring_replace_range(edit_astring_t *s, size_t beg, size_t end, const char *rep,
                                size_t replen);
size_t edit_astring_find(const edit_astring_t *s, const char *needle, size_t needlelen);
bool edit_astring_replace_once(edit_astring_t *s, const char *old, size_t oldlen, const char *rep,
                               size_t replen);
// 0 = valid (no copy, use input), 1 = lossy copy built in s, -1 = error.
int edit_astring_from_utf8_lossy(edit_astring_t *s, edit_arena_t *arena, const uint8_t *text,
                                 size_t len);

#ifdef __cplusplus
}
#endif

#endif
