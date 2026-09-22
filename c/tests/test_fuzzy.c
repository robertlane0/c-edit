#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "edit/arena.h"
#include "edit/fuzzy.h"

static int checks = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #cond);                                     \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static edit_arena_t g_arena;

static int expect_score(const char *hay, const char *ndl, bool nc, int32_t want_score,
                        const size_t *want_pos, size_t want_n) {
    int32_t score = -1;
    size_t *pos = (size_t *)0x1;
    size_t n = 999;
    int rc = edit_score_fuzzy(&g_arena, (const uint8_t *)hay, strlen(hay), (const uint8_t *)ndl,
                              strlen(ndl), nc, &score, &pos, &n);
    ++checks;
    if (rc != 0) {
        fprintf(stderr, "FAIL %d: rc %d for %s/%s\n", __LINE__, rc, hay, ndl);
        return 1;
    }
    ++checks;
    if (score != want_score || n != want_n) {
        fprintf(stderr, "FAIL %d: %s/%s nc=%d score %d (want %d) n %zu (want %zu)\n", __LINE__, hay,
                ndl, (int)nc, score, want_score, n, want_n);
        return 1;
    }
    for (size_t i = 0; i < want_n; ++i) {
        ++checks;
        if (pos == NULL || pos[i] != want_pos[i]) {
            fprintf(stderr, "FAIL %d: pos %zu mismatch\n", __LINE__, i);
            return 1;
        }
    }
    if (want_n == 0) {
        ++checks;
        if (pos != NULL) {
            fprintf(stderr, "FAIL %d: expected no positions\n", __LINE__);
            return 1;
        }
    }
    return 0;
}

int main(void) {
    CHECK(edit_arena_init(&g_arena, 1 << 20) == 0);

    // Vectors from Rust fuzzy::score_fuzzy (dead code validated in scratch copy).
    static const size_t p_fb[] = {0, 3};
    CHECK(expect_score("foobar", "fb", true, 12, p_fb, 2) == 0);
    CHECK(expect_score("foobar", "fb", false, 0, NULL, 0) == 0);
    CHECK(expect_score("FooBar", "fb", true, 12, p_fb, 2) == 0);
    static const size_t p_FB[] = {0, 3};
    CHECK(expect_score("FooBar", "FB", true, 14, p_FB, 2) == 0);
    static const size_t p_npe[] = {0, 4, 11};
    CHECK(expect_score("NullPointerException", "NPE", true, 18, p_npe, 3) == 0);
    static const size_t p_http[] = {0, 1, 2, 3};
    CHECK(expect_score("HTTPServer", "http", true, 42, p_http, 4) == 0);
    static const size_t p_main[] = {4, 5, 6, 7};
    CHECK(expect_score("src/main.rs", "main", true, 43, p_main, 4) == 0);
    CHECK(expect_score("src\\main.rs", "main", true, 43, p_main, 4) == 0);
    static const size_t p_de[] = {1, 2};
    CHECK(expect_score("ede", "de", true, 9, p_de, 2) == 0);
    CHECK(expect_score("abc", "xyz", true, 0, NULL, 0) == 0);
    CHECK(expect_score("ab", "abc", true, 0, NULL, 0) == 0);
    CHECK(expect_score("Straße", "strasse", true, 0, NULL, 0) == 0); // raw-len gate quirk
    static const size_t p_str[] = {0, 1, 2, 3, 4, 5};
    CHECK(expect_score("STRASSE", "straße", true, 89, p_str, 6) == 0);
    static const size_t p_aa[] = {0, 1};
    CHECK(expect_score("aaa", "aa", true, 17, p_aa, 2) == 0);
    static const size_t p_ace[] = {0, 2, 4};
    CHECK(expect_score("abcdef", "ace", true, 14, p_ace, 3) == 0);
    CHECK(expect_score("abcdef", "ace", false, 0, NULL, 0) == 0);
    static const size_t p_fbb[] = {1, 5, 9};
    CHECK(expect_score("_foo-bar.baz", "fbb", true, 18, p_fbb, 3) == 0);
    static const size_t p_full[] = {0, 1, 2, 3, 4, 5};
    CHECK(expect_score("foobar", "foobar", true, 95, p_full, 6) == 0);
    static const size_t p_x[] = {0};
    CHECK(expect_score("x", "x", true, 10, p_x, 1) == 0);
    CHECK(expect_score("axbxc", "abc", false, 0, NULL, 0) == 0);
    static const size_t p_abc[] = {0, 1, 2};
    CHECK(expect_score("abc", "abc", false, 29, p_abc, 3) == 0);
    static const size_t p_bcd[] = {1, 2, 3};
    CHECK(expect_score("aBcDeF", "bcd", true, 21, p_bcd, 3) == 0);
    CHECK(expect_score("aBcDeF", "bcd", false, 21, p_bcd, 3) == 0);

    // Empty inputs score zero.
    CHECK(expect_score("", "", false, 0, NULL, 0) == 0);
    CHECK(expect_score("abc", "", false, 0, NULL, 0) == 0);
    CHECK(expect_score("", "abc", false, 0, NULL, 0) == 0);

    // Bad args fail; partial outputs stay reset.
    int32_t sc = 5;
    size_t *pp = NULL;
    size_t nn = 5;
    CHECK(edit_score_fuzzy(NULL, (const uint8_t *)"a", 1, (const uint8_t *)"a", 1, true, &sc, &pp,
                           &nn) == -1);
    CHECK(edit_score_fuzzy(&g_arena, NULL, 1, (const uint8_t *)"a", 1, true, &sc, &pp, &nn) == -1);
    CHECK(edit_score_fuzzy(&g_arena, (const uint8_t *)"a", 1, (const uint8_t *)"a", 1, true, NULL,
                           &pp, &nn) == -1);

    edit_arena_destroy(&g_arena);
    printf("test_fuzzy: %d checks passed\n", checks);
    return 0;
}
