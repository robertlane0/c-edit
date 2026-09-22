#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "edit/doc.h"
#include "edit/gap.h"
#include "edit/icu.h"
#include "edit/uregex.h"

static int checks = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #cond);                                     \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static uint32_t gap_generation(const void *ctx) {
    return edit_gap_generation((const edit_gap_t *)ctx);
}

typedef struct {
    size_t beg;
    size_t end;
} match_t;

// Collects all matches; compares with Rust vectors.
static int expect_matches(edit_gap_t *g, const char *pat, int32_t flags, size_t reset,
                          const match_t *want, size_t nwant) {
    edit_doc_t doc;
    edit_gap_doc(g, &doc);
    edit_usrc_t src = {&doc, gap_generation, g};
    edit_utext_t text;
    if (edit_utext_init(&text, &src) != 0) {
        fprintf(stderr, "FAIL %d: utext init\n", __LINE__);
        return 1;
    }
    edit_regex_t rx;
    edit_error_t err;
    if (edit_regex_init(&rx, pat, strlen(pat), flags, &text, &err) != 0) {
        fprintf(stderr, "FAIL %d: regex init %s\n", __LINE__, pat);
        edit_utext_destroy(&text);
        return 1;
    }
    edit_regex_reset(&rx, reset);
    size_t n = 0;
    for (;;) {
        size_t b = 0;
        size_t e = 0;
        if (!edit_regex_next(&rx, &b, &e)) {
            break;
        }
        ++checks;
        if (n >= nwant || b != want[n].beg || e != want[n].end) {
            fprintf(stderr, "FAIL %d: %s match %zu = %zu..%zu\n", __LINE__, pat, n, b, e);
            edit_regex_destroy(&rx);
            edit_utext_destroy(&text);
            return 1;
        }
        ++n;
    }
    ++checks;
    if (n != nwant) {
        fprintf(stderr, "FAIL %d: %s got %zu matches, want %zu\n", __LINE__, pat, n, nwant);
        edit_regex_destroy(&rx);
        edit_utext_destroy(&text);
        return 1;
    }
    // set_text + reset still works after mutation.
    edit_gap_replace(g, 0, 0, (const uint8_t *)"", 0);
    edit_regex_set_text(&rx, &text);
    edit_regex_reset(&rx, 0);
    edit_regex_destroy(&rx);
    edit_utext_destroy(&text);
    return 0;
}

int main(void) {
    if (!edit_icu_available()) {
        fprintf(stderr, "note: libicu missing, skipping\n");
        printf("test_uregex: %d checks passed\n", checks);
        return 0;
    }
    edit_gap_t g;
    CHECK(edit_gap_init(&g, false) == 0);
    static const char *lines[] = {"hello world\n", "foo bar\n", "HELLO again\n",
                                  "0123 \xC3\xA9xample \xE2\x86\x92 \xE2\x9C\x93\n"};
    for (size_t i = 0; i < 4; ++i) {
        size_t len = edit_gap_len(&g);
        CHECK(edit_gap_replace(&g, len, len, (const uint8_t *)lines[i], strlen(lines[i])));
    }
    CHECK(edit_gap_len(&g) == 54);

    // Vectors from Rust icu::Regex over TextBuffer.
    static const match_t m_o[] = {{4, 5}, {7, 8}, {13, 15}};
    CHECK(expect_matches(&g, "o+", 0, 0, m_o, 3) == 0);
    static const match_t m_hi[] = {{0, 5}, {20, 25}};
    CHECK(expect_matches(&g, "hello", EDIT_REGEX_CASE_INSENSITIVE, 0, m_hi, 2) == 0);
    static const match_t m_foo[] = {{12, 15}};
    CHECK(expect_matches(&g, "^foo", EDIT_REGEX_MULTILINE, 0, m_foo, 1) == 0);
    CHECK(expect_matches(&g, "zzz", 0, 0, NULL, 0) == 0);
    static const match_t m_l[] = {{9, 10}, {43, 44}};
    CHECK(expect_matches(&g, "l+", 0, 5, m_l, 2) == 0);
    static const match_t m_d[] = {{32, 36}};
    CHECK(expect_matches(&g, "\\d+", 0, 0, m_d, 1) == 0);
    static const match_t m_e[] = {{37, 39}};
    CHECK(expect_matches(&g, "\xC3\xA9", 0, 0, m_e, 1) == 0);
    static const match_t m_arrow[] = {{46, 49}};
    CHECK(expect_matches(&g, "\xE2\x86\x92", 0, 0, m_arrow, 1) == 0);
    static const match_t m_w[] = {{6, 11}};
    CHECK(expect_matches(&g, "world", EDIT_REGEX_LITERAL, 0, m_w, 1) == 0);
    static const match_t m_a[] = {{17, 18}, {26, 27}, {28, 29}, {40, 41}};
    CHECK(expect_matches(&g, "a+", 0, 100, m_a, 4) == 0);
    static const match_t m_o8[] = {{13, 15}};
    CHECK(expect_matches(&g, "o+", 0, 8, m_o8, 1) == 0);

    // Bad pattern reports an Icu error.
    {
        edit_doc_t doc;
        edit_gap_doc(&g, &doc);
        edit_usrc_t src = {&doc, gap_generation, &g};
        edit_utext_t text;
        CHECK(edit_utext_init(&text, &src) == 0);
        edit_regex_t rx;
        edit_error_t err;
        CHECK(edit_regex_init(&rx, "(", 1, 0, &text, &err) != 0);
        CHECK(err.kind == EDIT_ERR_ICU);
        CHECK(edit_regex_init(&rx, "(", 1, 0, &text, NULL) != 0);
        edit_utext_destroy(&text);
    }

    // NULL safety.
    CHECK(edit_utext_init(NULL, NULL) != 0);
    edit_utext_destroy(NULL);
    CHECK(edit_regex_init(NULL, "a", 1, 0, NULL, NULL) != 0);
    edit_regex_destroy(NULL);
    {
        edit_regex_t rx;
        memset(&rx, 0, sizeof rx);
        size_t b = 9;
        size_t e = 9;
        CHECK(!edit_regex_next(&rx, &b, &e));
        CHECK(!edit_regex_next(NULL, NULL, NULL));
        edit_regex_set_text(NULL, NULL);
        edit_regex_set_text(&rx, NULL);
        edit_regex_reset(NULL, 0);
        edit_regex_reset(&rx, 0);
    }

    edit_gap_destroy(&g);
    printf("test_uregex: %d checks passed\n", checks);
    return 0;
}
