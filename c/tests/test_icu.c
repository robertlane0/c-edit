#include <stdbool.h>
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
    CHECK(edit_fold_case(small, sizeof small, NULL, 3) == (size_t)(-1));

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

    if (edit_icu_available()) {
        // Root collation vectors from Rust icu::compare_strings.
        CHECK(edit_compare_strings((const uint8_t *)"abc", 3, (const uint8_t *)"abd", 3) == -1);
        CHECK(edit_compare_strings((const uint8_t *)"abd", 3, (const uint8_t *)"abc", 3) == 1);
        CHECK(edit_compare_strings((const uint8_t *)"abc", 3, (const uint8_t *)"abc", 3) == 0);
        CHECK(edit_compare_strings((const uint8_t *)"a", 1, (const uint8_t *)"B", 1) == -1);
        CHECK(edit_compare_strings((const uint8_t *)"z", 1, (const uint8_t *)"\xC3\xA4", 2) == 1);
        CHECK(edit_compare_strings((const uint8_t *)"\xC3\xA4", 2, (const uint8_t *)"z", 1) == -1);
        CHECK(edit_compare_strings((const uint8_t *)"", 0, (const uint8_t *)"a", 1) == -1);
        CHECK(edit_compare_strings((const uint8_t *)"Stra\xC3\x9F"
                                                    "e",
                                   7, (const uint8_t *)"strasse", 7) == 1);
        CHECK(edit_compare_strings((const uint8_t *)"abc", 3, (const uint8_t *)"abcd", 4) == -1);

        // Error names and encodings.
        char name[64];
        CHECK(edit_icu_error_text(0, name, sizeof name) > 0); // U_ZERO_ERROR
        CHECK(strcmp(name, "U_ZERO_ERROR") == 0);
        CHECK(edit_icu_error_text(15, name, sizeof name) > 0); // BUFFER_OVERFLOW
        const char **encs = NULL;
        size_t nenc = edit_icu_encodings(&encs);
        CHECK(nenc > 1 && encs != NULL);
        bool utf8 = false;
        bool latin1 = false;
        for (size_t i = 0; i < nenc; ++i) {
            if (strcmp(encs[i], "UTF-8") == 0) {
                utf8 = true;
            }
            if (strcmp(encs[i], "ISO-8859-1") == 0) {
                latin1 = true;
            }
        }
        CHECK(utf8 && latin1);
        CHECK(edit_icu_encodings(NULL) == 0);

        // Converter round-trip UTF-8 -> UTF-16LE -> UTF-8.
        static const uint8_t text[] = "h\xC3\xA9llo w\xC3\xB6rld \xE2\x86\x92 \xE2\x9C\x93";
        uint16_t pivot[64];
        edit_conv_t c;
        CHECK(edit_conv_init(&c, "UTF-8", "UTF-16LE", pivot, 64) == 0);
        uint8_t mid[128];
        size_t in_used = 0;
        size_t out_used = 0;
        CHECK(edit_conv_step(&c, text, sizeof text - 1, mid, sizeof mid, &in_used, &out_used) == 0);
        CHECK(in_used == sizeof text - 1 && out_used == 30);
        static const uint8_t want_mid[] = {
            0x68, 0x00, 0xE9, 0x00, 0x6C, 0x00, 0x6C, 0x00, 0x6F, 0x00,
            0x20, 0x00, 0x77, 0x00, 0xF6, 0x00, 0x72, 0x00, 0x6C, 0x00,
            0x64, 0x00, 0x20, 0x00, 0x92, 0x21, 0x20, 0x00, 0x13, 0x27,
        };
        CHECK(memcmp(mid, want_mid, sizeof want_mid) == 0);
        edit_conv_destroy(&c);

        uint16_t pivot2[64];
        edit_conv_t c2;
        CHECK(edit_conv_init(&c2, "UTF-16LE", "UTF-8", pivot2, 64) == 0);
        uint8_t back[128];
        CHECK(edit_conv_step(&c2, mid, out_used, back, sizeof back, &in_used, &out_used) == 0);
        CHECK(out_used == sizeof text - 1 && memcmp(back, text, out_used) == 0);
        // Flush at end is harmless.
        CHECK(edit_conv_step(&c2, NULL, 0, back, sizeof back, &in_used, &out_used) == 0);
        edit_conv_destroy(&c2);

        // Small output buffers make partial progress without error.
        // Input may be fully absorbed into the pivot while output is short.
        uint16_t pivot3[64];
        edit_conv_t c3;
        CHECK(edit_conv_init(&c3, "UTF-8", "UTF-16LE", pivot3, 64) == 0);
        uint8_t tiny[4];
        CHECK(edit_conv_step(&c3, text, sizeof text - 1, tiny, sizeof tiny, &in_used, &out_used) ==
              0);
        CHECK(out_used == 4 && in_used > 0 && in_used <= sizeof text - 1);
        CHECK(memcmp(tiny, "\x68\x00\xE9\x00", 4) == 0);
        edit_conv_destroy(&c3);

        // Bad encodings fail with an Icu error.
        uint16_t pivot4[64];
        edit_conv_t c4;
        CHECK(edit_conv_init(&c4, "NOPE-9", "UTF-8", pivot4, 64) != 0);
        edit_error_t err;
        CHECK(edit_conv_error(&c4, &err) && err.kind == EDIT_ERR_ICU);
        edit_conv_destroy(&c4);
        CHECK(!edit_conv_error(&c4, NULL)); // destroyed: no error
        CHECK(edit_conv_init(NULL, "UTF-8", "UTF-8", pivot4, 64) != 0);
        CHECK(edit_conv_init(&c4, NULL, "UTF-8", pivot4, 64) != 0);
        CHECK(edit_conv_init(&c4, "UTF-8", "UTF-8", NULL, 0) != 0);
        CHECK(edit_conv_step(NULL, text, 1, back, sizeof back, NULL, NULL) != 0);
    } else {
        fprintf(stderr, "note: libicu missing, skipping converter tests\n");
    }

    printf("test_icu: %d checks passed\n", checks);
    return 0;
}
