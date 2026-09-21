#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "edit/arena.h"
#include "edit/astring.h"
#include "edit/utf8.h"

EDIT_AVEC_DEFINE(edit_av_u32, uint32_t)

static int checks = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #cond);                                     \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static int expect_bytes(const edit_astring_t *s, const char *want, size_t n) {
    ++checks;
    if (edit_astring_len(s) != n || memcmp(edit_astring_data(s), want, n) != 0) {
        fprintf(stderr, "FAIL %d: len %zu want %zu\n", __LINE__, edit_astring_len(s), n);
        return 1;
    }
    return 0;
}

// 0 = valid, 1 = lossy copy; compares against Rust ArenaString::from_utf8_lossy.
static int expect_lossy(edit_arena_t *arena, const uint8_t *text, size_t len, int want_rc,
                        const char *want, size_t wantlen) {
    edit_astring_t s;
    int rc = edit_astring_from_utf8_lossy(&s, arena, text, len);
    ++checks;
    if (rc != want_rc) {
        fprintf(stderr, "FAIL %d: rc %d want %d\n", __LINE__, rc, want_rc);
        return 1;
    }
    if (rc == 1 && expect_bytes(&s, want, wantlen) != 0) {
        return 1;
    }
    return 0;
}

int main(void) {
    edit_arena_t arena = {0};
    CHECK(edit_arena_init(&arena, 65536) == 0);

    // Generic vec works for wider types too.
    edit_av_u32_t u32v;
    edit_av_u32_init(&u32v, &arena);
    for (uint32_t i = 0; i < 100; ++i) {
        CHECK(edit_av_u32_push(&u32v, i * i));
    }
    CHECK(edit_av_u32_len(&u32v) == 100);
    for (uint32_t i = 0; i < 100; ++i) {
        CHECK(u32v.data[i] == i * i);
    }
    CHECK(edit_av_u32_capacity(&u32v) >= 100);
    static const uint32_t rep[] = {7, 7, 7};
    CHECK(edit_av_u32_replace_range(&u32v, 10, 20, rep, 3));
    CHECK(edit_av_u32_len(&u32v) == 93);
    CHECK(u32v.data[10] == 7 && u32v.data[12] == 7 && u32v.data[13] == 20 * 20);
    edit_av_u32_clear(&u32v);
    CHECK(edit_av_u32_len(&u32v) == 0);

    // Byte vec growth and replace_range clamping.
    edit_av_u8_t bv;
    edit_av_u8_init(&bv, &arena);
    for (int i = 0; i < 50; ++i) {
        CHECK(edit_av_u8_push(&bv, (uint8_t)i));
    }
    CHECK(edit_av_u8_len(&bv) == 50);
    static const uint8_t ins[] = {0xAA, 0xBB};
    CHECK(edit_av_u8_replace_range(&bv, 48, 1000, ins, 2)); // end clamps
    CHECK(edit_av_u8_len(&bv) == 50);
    CHECK(bv.data[48] == 0xAA && bv.data[49] == 0xBB);
    CHECK(edit_av_u8_replace_range(&bv, 60, 70, ins, 2)); // beg clamps: append
    CHECK(edit_av_u8_len(&bv) == 52);

    // String basics.
    edit_astring_t s;
    CHECK(edit_astring_from_str(&s, &arena, "hello", 5));
    CHECK(expect_bytes(&s, "hello", 5) == 0);
    CHECK(edit_astring_push_str(&s, " world", 6));
    CHECK(expect_bytes(&s, "hello world", 11) == 0);
    CHECK(edit_astring_push(&s, 0x20AC)); // €
    CHECK(edit_astring_len(&s) == 14);
    CHECK(!edit_astring_is_empty(&s));
    edit_astring_clear(&s);
    CHECK(edit_astring_is_empty(&s));

    // push_repeat ascii + multi-byte (quadratic path).
    CHECK(edit_astring_push_repeat(&s, 'x', 10));
    CHECK(expect_bytes(&s, "xxxxxxxxxx", 10) == 0);
    edit_astring_clear(&s);
    CHECK(edit_astring_push_repeat(&s, 0x20AC, 5));
    CHECK(edit_astring_len(&s) == 15);
    for (int i = 0; i < 5; ++i) {
        CHECK(memcmp(edit_astring_data(&s) + i * 3, "\xE2\x82\xAC", 3) == 0);
    }
    CHECK(edit_astring_push_repeat(&s, 'y', 0));
    CHECK(!edit_astring_push(&s, 0xD800));   // surrogate rejected
    CHECK(!edit_astring_push(&s, 0x110000)); // out of range rejected

    // replace_range / find / replace_once.
    edit_astring_clear(&s);
    CHECK(edit_astring_push_str(&s, "hello world", 11));
    CHECK(edit_astring_replace_range(&s, 6, 11, "there", 5));
    CHECK(expect_bytes(&s, "hello there", 11) == 0);
    CHECK(edit_astring_find(&s, "there", 5) == 6);
    CHECK(edit_astring_find(&s, "zzz", 3) == (size_t)-1);
    CHECK(edit_astring_find(&s, "", 0) == 0);
    CHECK(edit_astring_replace_once(&s, "l", 1, "L", 1));
    CHECK(expect_bytes(&s, "heLlo there", 11) == 0);
    CHECK(edit_astring_replace_once(&s, "zzz", 3, "q", 1)); // no match: unchanged
    CHECK(expect_bytes(&s, "heLlo there", 11) == 0);

    // with_capacity + reserve_exact + shrink_to_fit (tail).
    edit_astring_t t;
    CHECK(edit_astring_with_capacity(&t, &arena, 100));
    CHECK(edit_astring_capacity(&t) >= 100);
    CHECK(edit_astring_push_str(&t, "abc", 3));
    CHECK(edit_astring_shrink_to_fit(&t));
    CHECK(edit_astring_capacity(&t) == 3);
    CHECK(expect_bytes(&t, "abc", 3) == 0);

    // Lossy conversion: Rust-vector parity (Err bytes are UTF-8 of FFFDs).
    CHECK(expect_lossy(&arena, NULL, 0, 0, "", 0) == 0);
    CHECK(expect_lossy(&arena, (const uint8_t *)"", 0, 0, "", 0) == 0);
    CHECK(expect_lossy(&arena, (const uint8_t *)"abc", 3, 0, "", 0) == 0);
    static const uint8_t euro[] = {0xE2, 0x82, 0xAC};
    CHECK(expect_lossy(&arena, euro, 3, 0, "", 0) == 0);
    static const uint8_t fffd[] = {0xEF, 0xBF, 0xBD};
    CHECK(expect_lossy(&arena, fffd, 3, 0, "", 0) == 0); // literal U+FFFD is valid
    static const uint8_t lone[] = {0x80, 0xBF};
    CHECK(expect_lossy(&arena, lone, 2, 1, "\xEF\xBF\xBD\xEF\xBF\xBD", 6) == 0);
    static const uint8_t broken[] = {'a', 0xED, 0xA0, 0x80, 'b'};
    CHECK(expect_lossy(&arena, broken, 5, 1,
                       "a\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD"
                       "b",
                       11) == 0);
    static const uint8_t c0af[] = {0xC0, 0xAF};
    CHECK(expect_lossy(&arena, c0af, 2, 1, "\xEF\xBF\xBD\xEF\xBF\xBD", 6) == 0);
    static const uint8_t over[] = {0xE0, 0x80, 0x80};
    CHECK(expect_lossy(&arena, over, 3, 1, "\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD", 9) == 0);
    static const uint8_t trunc[] = {0xE0, 0xA0};
    CHECK(expect_lossy(&arena, trunc, 2, 1, "\xEF\xBF\xBD", 3) == 0);
    static const uint8_t mix[] = {0xFF, 0x41, 0x80};
    CHECK(expect_lossy(&arena, mix, 3, 1, "\xEF\xBF\xBD"
                                             "A\xEF\xBF\xBD",
                       7) == 0);
    static const uint8_t e0a028[] = {0xE0, 0xA0, 0x28};
    CHECK(expect_lossy(&arena, e0a028, 3, 1, "\xEF\xBF\xBD(", 4) == 0);
    static const uint8_t f0908028[] = {0xF0, 0x90, 0x80, 0x28};
    CHECK(expect_lossy(&arena, f0908028, 4, 1, "\xEF\xBF\xBD(", 4) == 0);
    // Error paths.
    edit_astring_t e;
    CHECK(edit_astring_from_utf8_lossy(NULL, &arena, euro, 3) == -1);
    CHECK(edit_astring_from_utf8_lossy(&e, &arena, NULL, 3) == -1);

    // UTF-8 encoder boundaries.
    char enc[4];
    CHECK(edit_utf8_encode(0x24, enc) == 1 && enc[0] == 0x24);
    CHECK(edit_utf8_encode(0xA9, enc) == 2 && memcmp(enc, "\xC2\xA9", 2) == 0);
    CHECK(edit_utf8_encode(0x20AC, enc) == 3 && memcmp(enc, "\xE2\x82\xAC", 3) == 0);
    CHECK(edit_utf8_encode(0x1D11E, enc) == 4 && memcmp(enc, "\xF0\x9D\x84\x9E", 4) == 0);
    CHECK(edit_utf8_encode(0xD800, enc) == 0);
    CHECK(edit_utf8_encode(0x110000, enc) == 0);
    CHECK(edit_utf8_encode(0x41, NULL) == 0);

    edit_arena_destroy(&arena);
    printf("test_astring: %d checks passed\n", checks);
    return 0;
}
