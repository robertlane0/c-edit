#include <stdint.h>
#include <stdio.h>

#include "edit/utf8.h"

static int checks = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #cond);                                     \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

// Decodes all of src, comparing codepoints and post-read offsets.
static int expect_seq(const uint8_t *src, size_t len, const uint32_t *cps, const size_t *offs,
                      size_t n) {
    edit_utf8_chars_t it;
    edit_utf8_chars_init(&it, src, len, 0);
    CHECK(edit_utf8_len(&it) == len);
    CHECK(edit_utf8_is_empty(&it) == (len == 0));
    for (size_t i = 0; i < n; ++i) {
        CHECK(edit_utf8_has_next(&it));
        uint32_t cp = 0;
        CHECK(edit_utf8_next(&it, &cp));
        ++checks;
        if (cp != cps[i] || edit_utf8_offset(&it) != offs[i]) {
            fprintf(stderr, "FAIL %d: step %zu got U+%04X@%zu\n", __LINE__, i, cp,
                    edit_utf8_offset(&it));
            return 1;
        }
    }
    CHECK(!edit_utf8_has_next(&it));
    uint32_t cp = 0;
    CHECK(!edit_utf8_next(&it, &cp)); // fused: stays exhausted
    return 0;
}

int main(void) {
    // Vectors from Rust unicode::Utf8Chars (codepoint, offset after read).
    static const uint8_t ascii[] = "Hi!";
    static const uint32_t ascii_cp[] = {0x48, 0x69, 0x21};
    static const size_t ascii_off[] = {1, 2, 3};
    CHECK(expect_seq(ascii, 3, ascii_cp, ascii_off, 3) == 0);

    static const uint8_t eacute[] = {0xC3, 0xA9};
    static const uint32_t eacute_cp[] = {0xE9};
    static const size_t eacute_off[] = {2};
    CHECK(expect_seq(eacute, 2, eacute_cp, eacute_off, 1) == 0);

    static const uint8_t euro[] = {0xE2, 0x82, 0xAC};
    static const uint32_t euro_cp[] = {0x20AC};
    static const size_t euro_off[] = {3};
    CHECK(expect_seq(euro, 3, euro_cp, euro_off, 1) == 0);

    static const uint8_t gclef[] = {0xF0, 0x9D, 0x84, 0x9E};
    static const uint32_t gclef_cp[] = {0x1D11E};
    static const size_t gclef_off[] = {4};
    CHECK(expect_seq(gclef, 4, gclef_cp, gclef_off, 1) == 0);

    static const uint8_t max[] = {0xF4, 0x8F, 0xBF, 0xBF};
    static const uint32_t max_cp[] = {0x10FFFF};
    static const size_t max_off[] = {4};
    CHECK(expect_seq(max, 4, max_cp, max_off, 1) == 0);

    // Port of Rust test_broken_utf8.
    static const uint8_t broken[] = {'a', 0xED, 0xA0, 0x80, 'b'};
    static const uint32_t broken_cp[] = {0x61, 0xFFFD, 0xFFFD, 0xFFFD, 0x62};
    static const size_t broken_off[] = {1, 2, 3, 4, 5};
    CHECK(expect_seq(broken, 5, broken_cp, broken_off, 5) == 0);

    static const uint8_t lone[] = {0x80, 0xBF};
    static const uint32_t lone_cp[] = {0xFFFD, 0xFFFD};
    static const size_t lone_off[] = {1, 2};
    CHECK(expect_seq(lone, 2, lone_cp, lone_off, 2) == 0);

    static const uint8_t c0c1[] = {0xC0, 0xAF, 0xC1, 0xBF};
    static const uint32_t c0c1_cp[] = {0xFFFD, 0xFFFD, 0xFFFD, 0xFFFD};
    static const size_t c0c1_off[] = {1, 2, 3, 4};
    CHECK(expect_seq(c0c1, 4, c0c1_cp, c0c1_off, 4) == 0);

    static const uint8_t overlong[] = {0xE0, 0x80, 0x80};
    static const uint32_t overlong_cp[] = {0xFFFD, 0xFFFD, 0xFFFD};
    static const size_t overlong_off[] = {1, 2, 3};
    CHECK(expect_seq(overlong, 3, overlong_cp, overlong_off, 3) == 0);

    static const uint8_t surr[] = {0xED, 0xA0, 0x80};
    static const uint32_t surr_cp[] = {0xFFFD, 0xFFFD, 0xFFFD};
    static const size_t surr_off[] = {1, 2, 3};
    CHECK(expect_seq(surr, 3, surr_cp, surr_off, 3) == 0);

    static const uint8_t edges[] = {0xED, 0x9F, 0xBF, 0xED, 0xA0, 0xBF};
    static const uint32_t edges_cp[] = {0xD7FF, 0xFFFD, 0xFFFD, 0xFFFD};
    static const size_t edges_off[] = {3, 4, 5, 6};
    CHECK(expect_seq(edges, 6, edges_cp, edges_off, 4) == 0);

    static const uint8_t f4r[] = {0xF4, 0x8F, 0xBF, 0xBF, 0xF4, 0x90, 0x80, 0x80};
    static const uint32_t f4r_cp[] = {0x10FFFF, 0xFFFD, 0xFFFD, 0xFFFD, 0xFFFD};
    static const size_t f4r_off[] = {4, 5, 6, 7, 8};
    CHECK(expect_seq(f4r, 8, f4r_cp, f4r_off, 5) == 0);

    static const uint8_t f5[] = {0xF5, 0xFF, 0xFE};
    static const uint32_t f5_cp[] = {0xFFFD, 0xFFFD, 0xFFFD};
    static const size_t f5_off[] = {1, 2, 3};
    CHECK(expect_seq(f5, 3, f5_cp, f5_off, 3) == 0);

    // Truncated tails consume what exists.
    static const uint8_t t2[] = {0xC2};
    static const uint32_t t2_cp[] = {0xFFFD};
    static const size_t t2_off[] = {1};
    CHECK(expect_seq(t2, 1, t2_cp, t2_off, 1) == 0);

    static const uint8_t t3a[] = {0xE0};
    static const uint32_t t3a_cp[] = {0xFFFD};
    static const size_t t3a_off[] = {1};
    CHECK(expect_seq(t3a, 1, t3a_cp, t3a_off, 1) == 0);

    static const uint8_t t3b[] = {0xE0, 0xA0};
    static const uint32_t t3b_cp[] = {0xFFFD};
    static const size_t t3b_off[] = {2};
    CHECK(expect_seq(t3b, 2, t3b_cp, t3b_off, 1) == 0);

    static const uint8_t t4a[] = {0xF0};
    static const uint32_t t4a_cp[] = {0xFFFD};
    static const size_t t4a_off[] = {1};
    CHECK(expect_seq(t4a, 1, t4a_cp, t4a_off, 1) == 0);

    static const uint8_t t4b[] = {0xF0, 0x90};
    static const uint32_t t4b_cp[] = {0xFFFD};
    static const size_t t4b_off[] = {2};
    CHECK(expect_seq(t4b, 2, t4b_cp, t4b_off, 1) == 0);

    static const uint8_t t4c[] = {0xF0, 0x90, 0x80};
    static const uint32_t t4c_cp[] = {0xFFFD};
    static const size_t t4c_off[] = {3};
    CHECK(expect_seq(t4c, 3, t4c_cp, t4c_off, 1) == 0);

    // Bad continuations resync after the consumed prefix.
    static const uint8_t bad2[] = {0xE0, 0x28, 0x80};
    static const uint32_t bad2_cp[] = {0xFFFD, 0x28, 0xFFFD};
    static const size_t bad2_off[] = {1, 2, 3};
    CHECK(expect_seq(bad2, 3, bad2_cp, bad2_off, 3) == 0);

    static const uint8_t bad3[] = {0xE0, 0xA0, 0x28};
    static const uint32_t bad3_cp[] = {0xFFFD, 0x28};
    static const size_t bad3_off[] = {2, 3};
    CHECK(expect_seq(bad3, 3, bad3_cp, bad3_off, 2) == 0);

    static const uint8_t bad4[] = {0xF0, 0x90, 0x80, 0x28};
    static const uint32_t bad4_cp[] = {0xFFFD, 0x28};
    static const size_t bad4_off[] = {3, 4};
    CHECK(expect_seq(bad4, 4, bad4_cp, bad4_off, 2) == 0);

    // Boundary codepoints.
    static const uint8_t bounds[] = {0xE0, 0xA0, 0x80, 0xF0, 0x9F, 0x98, 0x80};
    static const uint32_t bounds_cp[] = {0x0800, 0x1F600};
    static const size_t bounds_off[] = {3, 7};
    CHECK(expect_seq(bounds, 7, bounds_cp, bounds_off, 2) == 0);

    // Mixed valid/invalid.
    static const uint8_t mix[] = {0x41, 0xC3, 0xA9, 0xE2, 0x82, 0xAC,
                                  0xF0, 0x9D, 0x84, 0x9E, 0xFF, 0x5A};
    static const uint32_t mix_cp[] = {0x41, 0xE9, 0x20AC, 0x1D11E, 0xFFFD, 0x5A};
    static const size_t mix_off[] = {1, 3, 6, 10, 11, 12};
    CHECK(expect_seq(mix, 12, mix_cp, mix_off, 6) == 0);

    // Seek/has_next/empty/NULL handling.
    edit_utf8_chars_t it;
    edit_utf8_chars_init(&it, ascii, 3, 0);
    edit_utf8_seek(&it, 2);
    CHECK(edit_utf8_offset(&it) == 2);
    uint32_t cp = 0;
    CHECK(edit_utf8_next(&it, &cp) && cp == 0x21);
    edit_utf8_seek(&it, 99); // past end: exhausted, no crash
    CHECK(!edit_utf8_has_next(&it));
    CHECK(!edit_utf8_next(&it, &cp));
    edit_utf8_chars_init(&it, NULL, 0, 0);
    CHECK(edit_utf8_is_empty(&it));
    CHECK(!edit_utf8_next(&it, &cp));
    CHECK(!edit_utf8_next(NULL, &cp));
    CHECK(!edit_utf8_next(&it, NULL));
    CHECK(edit_utf8_len(NULL) == 0);
    CHECK(edit_utf8_offset(NULL) == 0);

    printf("test_utf8: %d checks passed\n", checks);
    return 0;
}
