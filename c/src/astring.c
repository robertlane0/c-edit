#include "edit/astring.h"

#include <assert.h>
#include <string.h>

#include "edit/utf8.h"

void edit_astring_init(edit_astring_t *s, edit_arena_t *arena) {
    if (s == NULL) {
        return;
    }
    edit_av_u8_init(&s->vec, arena);
}

bool edit_astring_with_capacity(edit_astring_t *s, edit_arena_t *arena, size_t capacity) {
    if (s == NULL) {
        return false;
    }
    edit_av_u8_init(&s->vec, arena);
    return edit_av_u8_reserve(&s->vec, capacity);
}

// Strict validator: accepts literal U+FFFD, rejects overlongs/surrogates.
static bool is_valid_strict(const uint8_t *text, size_t len) {
    size_t i = 0;
    while (i < len) {
        uint8_t c = text[i];
        if (c < 0x80) {
            ++i;
        } else if (c < 0xC2) {
            return false;
        } else if (c < 0xE0) {
            if (i + 1 >= len || (text[i + 1] & 0xC0U) != 0x80U) {
                return false;
            }
            i += 2;
        } else if (c < 0xF0) {
            if (i + 2 >= len || (text[i + 1] & 0xC0U) != 0x80U || (text[i + 2] & 0xC0U) != 0x80U) {
                return false;
            }
            uint32_t cp = ((uint32_t)(c & 0x0F) << 12) | ((uint32_t)(text[i + 1] & 0x3F) << 6) |
                          (uint32_t)(text[i + 2] & 0x3F);
            if (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF)) {
                return false;
            }
            i += 3;
        } else if (c < 0xF5) {
            if (i + 3 >= len || (text[i + 1] & 0xC0U) != 0x80U || (text[i + 2] & 0xC0U) != 0x80U ||
                (text[i + 3] & 0xC0U) != 0x80U) {
                return false;
            }
            uint32_t cp = ((uint32_t)(c & 0x07) << 18) | ((uint32_t)(text[i + 1] & 0x3F) << 12) |
                          ((uint32_t)(text[i + 2] & 0x3F) << 6) | (uint32_t)(text[i + 3] & 0x3F);
            if (cp < 0x10000 || cp > 0x10FFFF) {
                return false;
            }
            i += 4;
        } else {
            return false;
        }
    }
    return true;
}

// Debug-only validity scan; release trusts the caller like Rust &str does.
static bool is_valid_utf8(const char *str, size_t len) {
    if (len > 0 && str == NULL) {
        return false;
    }
    if (str == NULL) {
        return true;
    }
    return is_valid_strict((const uint8_t *)str, len);
}

bool edit_astring_from_str(edit_astring_t *s, edit_arena_t *arena, const char *str, size_t len) {
    if (s == NULL) {
        return false;
    }
    assert(is_valid_utf8(str, len));
    edit_av_u8_init(&s->vec, arena);
    return edit_av_u8_extend(&s->vec, (const uint8_t *)str, len);
}

size_t edit_astring_len(const edit_astring_t *s) {
    return s == NULL ? 0 : s->vec.len;
}

size_t edit_astring_capacity(const edit_astring_t *s) {
    return s == NULL ? 0 : s->vec.cap;
}

bool edit_astring_is_empty(const edit_astring_t *s) {
    return edit_astring_len(s) == 0;
}

const char *edit_astring_data(const edit_astring_t *s) {
    if (s == NULL || s->vec.data == NULL) {
        return NULL;
    }
    return (const char *)s->vec.data;
}

bool edit_astring_reserve(edit_astring_t *s, size_t additional) {
    if (s == NULL) {
        return false;
    }
    return edit_av_u8_reserve(&s->vec, additional);
}

bool edit_astring_reserve_exact(edit_astring_t *s, size_t additional) {
    // Arena has no overallocation knob; exactness comes free.
    return edit_astring_reserve(s, additional);
}

bool edit_astring_shrink_to_fit(edit_astring_t *s) {
    if (s == NULL) {
        return false;
    }
    return edit_av_u8_shrink_to_fit(&s->vec);
}

void edit_astring_clear(edit_astring_t *s) {
    if (s != NULL) {
        edit_av_u8_clear(&s->vec);
    }
}

bool edit_astring_push_str(edit_astring_t *s, const char *str, size_t len) {
    if (s == NULL) {
        return false;
    }
    assert(is_valid_utf8(str, len));
    if (len == 0) {
        return true;
    }
    return edit_av_u8_extend(&s->vec, (const uint8_t *)str, len);
}

bool edit_astring_push(edit_astring_t *s, uint32_t cp) {
    if (s == NULL) {
        return false;
    }
    char buf[4];
    size_t n = edit_utf8_encode(cp, buf);
    if (n == 0) {
        return false;
    }
    return edit_av_u8_extend(&s->vec, (const uint8_t *)buf, n);
}

bool edit_astring_push_repeat(edit_astring_t *s, uint32_t cp, size_t n) {
    if (s == NULL) {
        return false;
    }
    if (n == 0) {
        return true;
    }
    char buf[4];
    size_t clen = edit_utf8_encode(cp, buf);
    if (clen == 0) {
        return false;
    }
    if (clen == 1) {
        // ASCII fast path via fill.
        if (!edit_av_u8_reserve(&s->vec, n)) {
            return false;
        }
        memset(s->vec.data + s->vec.len, buf[0], n);
        s->vec.len += n;
        return true;
    }
    // Quadratic duplication like Rust push_repeat.
    size_t initial = s->vec.len;
    if (n > ((size_t)-1 - initial) / clen) {
        return false;
    }
    size_t final_len = initial + clen * n;
    if (!edit_av_u8_extend(&s->vec, (const uint8_t *)buf, clen)) {
        return false;
    }
    while (s->vec.len != final_len) {
        size_t end = initial + (final_len - s->vec.len);
        if (end > s->vec.len) {
            end = s->vec.len;
        }
        size_t chunk = end - initial;
        if (!edit_av_u8_extend(&s->vec, s->vec.data + initial, chunk)) {
            return false;
        }
    }
    return true;
}

static bool is_char_boundary(const edit_astring_t *s, size_t pos) {
    if (pos == 0 || pos == s->vec.len) {
        return true;
    }
    if (pos > s->vec.len) {
        return false;
    }
    return (s->vec.data[pos] & 0xC0U) != 0x80U;
}

bool edit_astring_replace_range(edit_astring_t *s, size_t beg, size_t end, const char *rep,
                                size_t replen) {
    if (s == NULL) {
        return false;
    }
    assert(is_char_boundary(s, beg) && is_char_boundary(s, end > s->vec.len ? s->vec.len : end));
    assert(is_valid_utf8(rep, replen));
    return edit_av_u8_replace_range(&s->vec, beg, end, (const uint8_t *)rep, replen);
}

size_t edit_astring_find(const edit_astring_t *s, const char *needle, size_t needlelen) {
    if (s == NULL || needle == NULL) {
        return (size_t)-1;
    }
    if (needlelen == 0) {
        return 0;
    }
    if (needlelen > s->vec.len) {
        return (size_t)-1;
    }
    const uint8_t *hay = s->vec.data;
    size_t limit = s->vec.len - needlelen;
    size_t i = 0;
    while (i <= limit) {
        const void *hit = memchr(hay + i, (uint8_t)needle[0], limit - i + 1);
        if (hit == NULL) {
            return (size_t)-1;
        }
        i = (size_t)((const uint8_t *)hit - hay);
        if (memcmp(hay + i, needle, needlelen) == 0) {
            return i;
        }
        ++i;
    }
    return (size_t)-1;
}

bool edit_astring_replace_once(edit_astring_t *s, const char *old, size_t oldlen, const char *rep,
                               size_t replen) {
    if (s == NULL) {
        return false;
    }
    size_t at = edit_astring_find(s, old, oldlen);
    if (at == (size_t)-1) {
        return true; // no match, like Rust
    }
    return edit_astring_replace_range(s, at, at + oldlen, rep, replen);
}

int edit_astring_from_utf8_lossy(edit_astring_t *s, edit_arena_t *arena, const uint8_t *text,
                                 size_t len) {
    if (s == NULL) {
        return -1;
    }
    if (len > 0 && text == NULL) {
        return -1;
    }
    if (text != NULL && is_valid_strict(text, len)) {
        return 0; // fully valid: no copy needed
    }
    if (text == NULL) {
        return 0; // empty input is valid
    }
    // Lossy path: decode stepwise, one FFFD per error (matches Rust).
    edit_av_u8_init(&s->vec, arena);
    if (!edit_av_u8_reserve(&s->vec, len)) {
        return -1;
    }
    edit_utf8_chars_t it;
    edit_utf8_chars_init(&it, text, len, 0);
    uint32_t cp = 0;
    char buf[4];
    while (edit_utf8_next(&it, &cp)) {
        size_t n = edit_utf8_encode(cp, buf);
        if (n == 0 || !edit_av_u8_extend(&s->vec, (const uint8_t *)buf, n)) {
            return -1;
        }
    }
    return 1;
}
