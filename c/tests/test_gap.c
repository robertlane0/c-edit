#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "edit/doc.h"
#include "edit/gap.h"
#include "edit/nav.h"

static int checks = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #cond);                                     \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static int expect_text(edit_gap_t *g, const char *want) {
    size_t n = strlen(want);
    uint8_t buf[256];
    memset(buf, 0xAA, sizeof buf);
    size_t got = edit_gap_extract(g, 0, (size_t)(-1), buf, sizeof buf);
    ++checks;
    if (got != n || memcmp(buf, want, n) != 0) {
        fprintf(stderr, "FAIL %d: got %zu want %zu\n", __LINE__, got, n);
        return 1;
    }
    CHECK(edit_gap_len(g) == n);
    return 0;
}

static int build(edit_gap_t *g, const char *text) {
    if (!edit_gap_replace(g, 0, (size_t)(-1), (const uint8_t *)text, strlen(text))) {
        fprintf(stderr, "FAIL %d: build failed\n", __LINE__);
        return 1;
    }
    ++checks;
    return expect_text(g, text);
}

int main(void) {
    for (int small = 0; small <= 1; ++small) {
        edit_gap_t g;
        CHECK(edit_gap_init(&g, small != 0) == 0);
        CHECK(edit_gap_len(&g) == 0);
        CHECK(edit_gap_generation(&g) == 0);

        // Build up text with inserts at both ends and middle.
        CHECK(build(&g, "Hello World") == 0);
        uint32_t gen = edit_gap_generation(&g);
        CHECK(gen != 0);
        CHECK(edit_gap_replace(&g, 5, 5, (const uint8_t *)",", 1));
        CHECK(expect_text(&g, "Hello, World") == 0);
        CHECK(edit_gap_replace(&g, 0, 0, (const uint8_t *)"Say: ", 5));
        CHECK(expect_text(&g, "Say: Hello, World") == 0);
        CHECK(edit_gap_replace(&g, 17, 17, (const uint8_t *)"!", 1));
        CHECK(expect_text(&g, "Say: Hello, World!") == 0);
        // Delete a range.
        CHECK(edit_gap_replace(&g, 0, 5, NULL, 0));
        CHECK(expect_text(&g, "Hello, World!") == 0);
        // Overwrite + shrink tail.
        CHECK(edit_gap_replace(&g, 7, 12, (const uint8_t *)"Rust", 4));
        CHECK(expect_text(&g, "Hello, Rust!") == 0);
        CHECK(edit_gap_generation(&g) != gen);
        edit_gap_set_generation(&g, 42);
        CHECK(edit_gap_generation(&g) == 42);

        // Read chunks see around the gap wherever it sits.
        {
            const uint8_t *p = NULL;
            size_t n = 0;
            edit_gap_read_fwd(&g, 0, &p, &n);
            CHECK(n > 0 && memcmp(p, "Hello, Rust!", n < 12 ? n : 12) == 0);
            edit_gap_read_fwd(&g, 7, &p, &n);
            CHECK(n == 4 && memcmp(p, "Rust", 4) == 0); // stops at the gap
            edit_gap_read_bwd(&g, 12, &p, &n);
            CHECK(n == 1 && memcmp(p, "!", 1) == 0); // tail past the gap
            edit_gap_read_bwd(&g, 5, &p, &n);
            CHECK(n == 5 && memcmp(p, "Hello", 5) == 0);
            // Clamped offsets.
            edit_gap_read_fwd(&g, 999, &p, &n);
            CHECK(n == 0);
            edit_gap_read_bwd(&g, 999, &p, &n);
            CHECK(n == 1); // clamped to len, same tail chunk
        }

        // Partial extract.
        {
            uint8_t buf[16];
            CHECK(edit_gap_extract(&g, 7, 11, buf, sizeof buf) == 4);
            CHECK(memcmp(buf, "Rust", 4) == 0);
            CHECK(edit_gap_extract(&g, 0, 99, buf, 4) == 4);
            CHECK(memcmp(buf, "Hell", 4) == 0);
        }

        // Word navigation works over the gap doc.
        {
            edit_doc_t doc;
            edit_gap_doc(&g, &doc);
            CHECK(edit_word_forward(&doc, 0) == 5);
            CHECK(edit_word_backward(&doc, 12) == 7);
            size_t b = 0;
            size_t e = 0;
            edit_word_select(&doc, 8, &b, &e);
            CHECK(b == 7 && e == 11);
        }

        // Clear keeps capacity for reuse.
        edit_gap_clear(&g);
        CHECK(edit_gap_len(&g) == 0);
        CHECK(build(&g, "second life") == 0);

        // copy_from: identical -> false, changed -> true.
        {
            edit_slice_doc_t src;
            edit_slice_doc_init(&src, (const uint8_t *)"second life", 11);
            CHECK(!edit_gap_copy_from(&g, &src.doc));
            edit_slice_doc_init(&src, (const uint8_t *)"second lifer", 12);
            CHECK(edit_gap_copy_from(&g, &src.doc));
            CHECK(expect_text(&g, "second lifer") == 0);
            edit_slice_doc_init(&src, (const uint8_t *)"", 0);
            CHECK(edit_gap_copy_from(&g, &src.doc));
            CHECK(edit_gap_len(&g) == 0);
            CHECK(!edit_gap_copy_from(&g, &src.doc)); // both empty
        }

        // copy_into another gap.
        {
            CHECK(build(&g, "copy me") == 0);
            edit_gap_t dst;
            CHECK(edit_gap_init(&dst, small != 0) == 0);
            edit_doc_t ddoc;
            edit_gap_doc(&dst, &ddoc);
            CHECK(edit_gap_copy_into(&g, &ddoc));
            CHECK(expect_text(&dst, "copy me") == 0);
            // Empty source clears destination.
            edit_gap_clear(&g);
            CHECK(edit_gap_copy_into(&g, &ddoc));
            CHECK(edit_gap_len(&dst) == 0);
            edit_gap_destroy(&dst);
        }

        edit_gap_destroy(&g);
    }

    // Oversize request on a small buffer degrades gracefully.
    {
        edit_gap_t g;
        CHECK(edit_gap_init(&g, true) == 0);
        size_t gap = 0;
        uint8_t *p = edit_gap_allocate(&g, 0, (size_t)(-1), 0, &gap);
        CHECK(p == NULL || gap < (size_t)(-1));
        CHECK(edit_gap_replace(&g, 0, 0, (const uint8_t *)"x", 1));
        CHECK(expect_text(&g, "x") == 0);
        edit_gap_destroy(&g);
    }

    // Bad args are safe.
    CHECK(edit_gap_init(NULL, false) == -1);
    CHECK(edit_gap_len(NULL) == 0);
    CHECK(edit_gap_generation(NULL) == 0);
    edit_gap_set_generation(NULL, 1);
    edit_gap_destroy(NULL);
    edit_gap_clear(NULL);
    CHECK(!edit_gap_replace(NULL, 0, 1, (const uint8_t *)"x", 1));
    {
        edit_gap_t g;
        CHECK(edit_gap_init(&g, true) == 0);
        CHECK(!edit_gap_replace(&g, 0, 1, NULL, 1));
        size_t n = 0;
        CHECK(edit_gap_allocate(NULL, 0, 1, 0, &n) == NULL);
        CHECK(edit_gap_allocate(&g, 0, 1, 0, NULL) == NULL);
        CHECK(edit_gap_extract(NULL, 0, 1, NULL, 0) == 0);
        CHECK(!edit_gap_copy_from(NULL, NULL));
        CHECK(!edit_gap_copy_into(NULL, NULL));
        edit_doc_t doc;
        edit_gap_doc(NULL, &doc);
        edit_gap_doc(&g, NULL);
        edit_gap_doc(&g, &doc);
        CHECK(edit_doc_len(&doc) == 0);
        edit_gap_destroy(&g);
    }

    printf("test_gap: %d checks passed\n", checks);
    return 0;
}
