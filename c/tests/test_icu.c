#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "edit/icu.h"

static int checks = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #cond);                                     \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static int expect_fold(const char *src, const char *want) {
    char buf[128];
    size_t slen = strlen(src);
    size_t wlen = strlen(want);
    size_t need = edit_fold_case(buf, sizeof buf, src, slen);
    ++checks;
    if (need != wlen) {
        fprintf(stderr, "FAIL %d: fold %s need %zu != %zu\n", __LINE__, src, need, wlen);
        return 1;
    }
    ++checks;
    if (memcmp(buf, want, wlen) != 0 || buf[wlen] != '\0') {
        fprintf(stderr, "FAIL %d: fold %s mismatch\n", __LINE__, src);
        return 1;
    }
    ++checks;
    if (edit_fold_case(NULL, 0, src, slen) != wlen) {
        fprintf(stderr, "FAIL %d: query mismatch for %s\n", __LINE__, src);
        return 1;
    }
    return 0;
}

static int expect_cmp(const uint8_t *a, size_t al, const uint8_t *b, size_t bl, int want) {
    int got = edit_compare_ascii(a, al, b, bl);
    ++checks;
    if (got != want) {
        fprintf(stderr, "FAIL %d: cmp got %d want %d\n", __LINE__, got, want);
        return 1;
    }
    return 0;
}

int main(void) {
    // ASCII folding works with or without libicu.
    CHECK(expect_fold("", "") == 0);
    CHECK(expect_fold("ABC", "abc") == 0);
    CHECK(expect_fold("Hello World", "hello world") == 0);
    CHECK(expect_fold("aBcDeF123!@#", "abcdef123!@#") == 0);
    CHECK(expect_fold("TURKCE", "turkce") == 0);

    if (edit_icu_available()) {
        // Full Unicode folding (vectors from Rust icu::fold_case + libicu).
        CHECK(expect_fold("Straße", "strasse") == 0);
        CHECK(expect_fold("ΩΜΕΓΑ", "ωμεγα") == 0);
        CHECK(expect_fold("ﬁle", "file") == 0);
        CHECK(expect_fold("Σίσυφος", "σίσυφοσ") == 0);
        CHECK(expect_fold("Iİı", "ii̇ı") == 0);
        CHECK(expect_fold("ÄÖÜ äöü ß", "äöü äöü ss") == 0);
        // Folding can expand: İ (2 bytes) -> i + ◌̇ (3 bytes).
        char big[64];
        CHECK(edit_fold_case(big, sizeof big, "İ", 2) == 3);
    } else {
        fprintf(stderr, "note: libicu missing, testing ASCII fallback\n");
        CHECK(expect_fold("Straße", "straße") == 0);
    }

    // Truncation reports full length and NUL-terminates.
    char small[4];
    CHECK(edit_fold_case(small, sizeof small, "ABCDEF", 6) == 6);
    CHECK(small[3] == '\0' && memcmp(small, "abc", 3) == 0);

    // Bad args.
    CHECK(edit_fold_case(small, sizeof small, NULL, 3) == (size_t)-1);

    // Port of Rust test_compare_strings_ascii.
    CHECK(expect_cmp((const uint8_t *)"", 0, (const uint8_t *)"", 0, 0) == 0);
    CHECK(expect_cmp((const uint8_t *)"hello", 5, (const uint8_t *)"hello", 5, 0) == 0);
    CHECK(expect_cmp((const uint8_t *)"abc", 3, (const uint8_t *)"abcd", 4, -1) == 0);
    CHECK(expect_cmp((const uint8_t *)"abcd", 4, (const uint8_t *)"abc", 3, 1) == 0);
    CHECK(expect_cmp((const uint8_t *)"AbC", 3, (const uint8_t *)"aBc", 3, -1) == 0);
    CHECK(expect_cmp((const uint8_t *)"hallo", 5, (const uint8_t *)"Hello", 5, -1) == 0);
    CHECK(expect_cmp((const uint8_t *)"Hello", 5, (const uint8_t *)"hallo", 5, 1) == 0);
    // NULL handling.
    CHECK(edit_compare_ascii(NULL, 0, NULL, 0) == 0);
    CHECK(edit_compare_ascii(NULL, 0, (const uint8_t *)"a", 1) == -1);
    CHECK(edit_compare_ascii((const uint8_t *)"a", 1, NULL, 0) == 1);

    printf("test_icu: %d checks passed\n", checks);
    return 0;
}
