#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "edit/fb.h"
#include "edit/helpers.h"
#include "edit/uitext.h"

static int checks = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #cond);                                     \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static int expect_render(edit_fb_t *fb, const char *want) {
    const char *got = NULL;
    size_t n = edit_fb_render(fb, &got);
    size_t wlen = strlen(want);
    ++checks;
    if (n != wlen || got == NULL || memcmp(got, want, n) != 0) {
        fprintf(stderr, "FAIL %d: render len %zu != %zu\n", __LINE__, n, wlen);
        if (got != NULL) {
            fprintf(stderr, "got: %.*s\n", (int)(n > 200 ? 200 : n), got);
        }
        return 1;
    }
    return 0;
}

static edit_rect_t rect(int l, int t, int r, int b) {
    edit_rect_t rc = {l, t, r, b};
    return rc;
}

static edit_size_t sz(int w, int h) {
    edit_size_t s = {w, h};
    return s;
}

int main(void) {
    // Measure helper sanity (Rust vectors: wide text maps).
    CHECK(edit_text_measure("hello", 5) == 5);
    CHECK(edit_text_measure("a\xE2\x86\x92"
                            "b\xE2\x9C\x93"
                            "c",
                            9) == 5);
    CHECK(edit_text_measure("", 0) == 0);
    CHECK(edit_text_measure(NULL, 0) == 0);

    // Plain clip into a 20-wide target.
    {
        edit_fb_t fb;
        CHECK(edit_fb_init(&fb) == 0);
        CHECK(edit_fb_flip(&fb, sz(20, 3)) == 0);
        edit_text_content_t c;
        edit_text_init(&c);
        CHECK(edit_text_add(&c, "hello world", 11));
        CHECK(edit_text_render(&c, rect(0, 0, 20, 1), 11, &fb));
        CHECK(expect_render(&fb,
                            "\x1b[m\x1b[1;1H\x1b[48;2;0;0;0m\x1b[38;2;255;255;255mhello world"
                            "         \x1b[2;1H                    \x1b[3;1H                    "
                            "\x1b[?25l") == 0);
        edit_text_destroy(&c);
        edit_fb_destroy(&fb);
    }

    // Tail truncation: "hello bra" + … (Rust-computed split at 9).
    {
        edit_fb_t fb;
        CHECK(edit_fb_init(&fb) == 0);
        CHECK(edit_fb_flip(&fb, sz(10, 1)) == 0);
        edit_text_content_t c;
        edit_text_init(&c);
        static const char *t = "hello brave new world, this is long";
        CHECK(edit_text_add(&c, t, 35));
        edit_text_set_overflow(&c, EDIT_OVF_TAIL);
        CHECK(edit_text_render(&c, rect(0, 0, 10, 1), 35, &fb));
        CHECK(expect_render(&fb, "\x1b[m\x1b[1;1H\x1b[48;2;0;0;0m\x1b[38;2;255;255;255mhello bra"
                                 "\xE2\x80\xA6\x1b[?25l") == 0);
        edit_text_destroy(&c);
        edit_fb_destroy(&fb);
    }

    // Head truncation: "…" + "s is long".
    {
        edit_fb_t fb;
        CHECK(edit_fb_init(&fb) == 0);
        CHECK(edit_fb_flip(&fb, sz(10, 1)) == 0);
        edit_text_content_t c;
        edit_text_init(&c);
        static const char *t = "hello brave new world, this is long";
        CHECK(edit_text_add(&c, t, 35));
        edit_text_set_overflow(&c, EDIT_OVF_HEAD);
        CHECK(edit_text_render(&c, rect(0, 0, 10, 1), 35, &fb));
        CHECK(expect_render(&fb,
                            "\x1b[m\x1b[1;1H\x1b[48;2;0;0;0m\x1b[38;2;255;255;255m\xE2\x80\xA6s "
                            "is long\x1b[?25l") == 0);
        edit_text_destroy(&c);
        edit_fb_destroy(&fb);
    }

    // Middle truncation: "hell" + … + " long".
    {
        edit_fb_t fb;
        CHECK(edit_fb_init(&fb) == 0);
        CHECK(edit_fb_flip(&fb, sz(10, 1)) == 0);
        edit_text_content_t c;
        edit_text_init(&c);
        static const char *t = "hello brave new world, this is long";
        CHECK(edit_text_add(&c, t, 35));
        edit_text_set_overflow(&c, EDIT_OVF_MIDDLE);
        CHECK(edit_text_render(&c, rect(0, 0, 10, 1), 35, &fb));
        CHECK(expect_render(&fb, "\x1b[m\x1b[1;1H\x1b[48;2;0;0;0m\x1b[38;2;255;255;255mhell"
                                 "\xE2\x80\xA6 long\x1b[?25l") == 0);
        edit_text_destroy(&c);
        edit_fb_destroy(&fb);
    }

    // Styled chunks: red fg on [0,9), green on [9,11).
    {
        edit_fb_t fb;
        CHECK(edit_fb_init(&fb) == 0);
        CHECK(edit_fb_flip(&fb, sz(20, 1)) == 0);
        edit_text_content_t c;
        edit_text_init(&c);
        CHECK(edit_text_set_fg(&c, 0xFFFF0000));
        CHECK(edit_text_add(&c, "hello", 5));
        CHECK(edit_text_add(&c, " wor", 4));
        CHECK(edit_text_set_fg(&c, 0xFF00FF00));
        CHECK(edit_text_add(&c, "ld", 2));
        CHECK(c.nchunks == 2);
        CHECK(c.chunks[0].offset == 0 && c.chunks[0].fg == 0xFFFF0000);
        CHECK(c.chunks[1].offset == 9 && c.chunks[1].fg == 0xFF00FF00);
        edit_text_set_overflow(&c, EDIT_OVF_CLIP);
        CHECK(edit_text_render(&c, rect(0, 0, 20, 1), 11, &fb));
        CHECK(expect_render(&fb, "\x1b[m\x1b[1;1H\x1b[48;2;0;0;0m\x1b[38;2;0;0;255mhello wor"
                                 "\x1b[38;2;0;255;0mld\x1b[38;2;255;255;255m         "
                                 "\x1b[?25l") == 0);
        edit_text_destroy(&c);
        edit_fb_destroy(&fb);
    }
    // Wide text clips by columns, not bytes (→ and ✓ are narrow here).
    {
        edit_fb_t fb;
        CHECK(edit_fb_init(&fb) == 0);
        CHECK(edit_fb_flip(&fb, sz(4, 1)) == 0);
        edit_text_content_t c;
        edit_text_init(&c);
        CHECK(edit_text_add(&c,
                            "a\xE2\x86\x92"
                            "b\xE2\x9C\x93"
                            "c",
                            9));
        CHECK(edit_text_render(&c, rect(0, 0, 4, 1), 5, &fb));
        CHECK(expect_render(&fb, "\x1b[m\x1b[1;1H\x1b[48;2;0;0;0m\x1b[38;2;255;255;255ma"
                                 "\xE2\x86\x92"
                                 "b\xE2\x9C\x93\x1b[?25l") == 0);
        edit_text_destroy(&c);
        edit_fb_destroy(&fb);
    }

    // NULL safety.
    edit_text_init(NULL);
    edit_text_destroy(NULL);
    CHECK(!edit_text_add(NULL, "x", 1));
    CHECK(!edit_text_set_fg(NULL, 1));
    CHECK(!edit_text_set_attr(NULL, 1));
    edit_text_set_overflow(NULL, EDIT_OVF_CLIP);
    CHECK(edit_text_measure(NULL, 5) == 0);
    CHECK(!edit_text_render(NULL, rect(0, 0, 1, 1), 0, NULL));

    printf("test_uitext: %d checks passed\n", checks);
    return 0;
}
