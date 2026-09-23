#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "edit/fb.h"
#include "edit/helpers.h"
#include "edit/tbuf.h"

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
            fprintf(stderr, "got: %.*s\n", (int)(n > 300 ? 300 : n), got);
        }
        return 1;
    }
    return 0;
}

static edit_point_t pt(int x, int y) {
    edit_point_t p = {x, y};
    return p;
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
    edit_tbuf_t tb;
    CHECK(edit_tbuf_init(&tb, false) == 0);
    edit_tbuf_write(&tb, (const uint8_t *)"hello w\xC3\xB6rld\nfoo\tbar\nline three here", 36,
                    false);
    CHECK(edit_tbuf_set_width(&tb, 20));
    edit_fb_t fb;
    CHECK(edit_fb_init(&fb) == 0);
    CHECK(edit_fb_flip(&fb, sz(20, 6)) == 0);

    // Vectors from Rust TextBuffer::render + framebuffer.
    int32_t xmax = -1;
    CHECK(edit_tbuf_render(&tb, pt(0, 0), rect(0, 0, 20, 6), true, &fb, &xmax));
    CHECK(xmax == 15);
    CHECK(expect_render(
              &fb, "\x1b[m\x1b[1;1H\x1b[48;2;0;0;0m\x1b[38;2;255;255;255mhello w\xC3\xB6rld"
                   "         \x1b[2;1Hfoo bar             \x1b[3;1Hline three here     "
                   "\x1b[4;1H                    \x1b[5;1H                    \x1b[6;1H         "
                   "           \x1b[3;16H\x1b[5 q\x1b[?25h") == 0);

    edit_tbuf_goto_logical(&tb, pt(2, 0));
    edit_tbuf_select_word(&tb);
    CHECK(edit_fb_flip(&fb, sz(20, 6)) == 0);
    CHECK(edit_tbuf_render(&tb, pt(0, 0), rect(0, 0, 20, 6), true, &fb, &xmax));
    CHECK(xmax == 15);
    CHECK(expect_render(&fb, "\x1b[m\x1b[1;1H\x1b[48;2;165;206;255m\x1b[38;2;0;0;0mhello"
                             "\x1b[48;2;0;0;0m\x1b[38;2;255;255;255m w\xC3\xB6rld         "
                             "\x1b[1;6H\x1b[5 q\x1b[?25h") == 0);

    edit_tbuf_set_margin(&tb, true);
    edit_tbuf_set_ruler(&tb, 10);
    CHECK(edit_fb_flip(&fb, sz(20, 6)) == 0);
    CHECK(edit_tbuf_render(&tb, pt(3, 1), rect(0, 0, 20, 6), false, &fb, &xmax));
    CHECK(xmax == 15);
    CHECK(expect_render(&fb, "\x1b[m\x1b[1;1H\x1b[48;2;0;0;0m\x1b[38;2;211;211;211m2 \xE2\x94\x82 "
                             "\x1b[38;2;255;255;255m bar   \x1b[48;2;63;15;12m         "
                             "\x1b[2;1H\x1b[48;2;0;0;0m\x1b[38;2;211;211;211m3 \xE2\x94\x82 "
                             "\x1b[38;2;255;255;255me three\x1b[48;2;63;15;12m here    "
                             "\x1b[3;1H\x1b[48;2;0;0;0m\x1b[38;2;211;211;211m  \xE2\x94\x82 "
                             "\x1b[38;2;255;255;255m       \x1b[48;2;63;15;12m         "
                             "\x1b[4;1H\x1b[48;2;0;0;0m\x1b[38;2;211;211;211m  \xE2\x94\x82 "
                             "\x1b[38;2;255;255;255m       \x1b[48;2;63;15;12m         "
                             "\x1b[5;1H\x1b[48;2;0;0;0m\x1b[38;2;211;211;211m  \xE2\x94\x82 "
                             "\x1b[38;2;255;255;255m       \x1b[48;2;63;15;12m         "
                             "\x1b[6;1H\x1b[48;2;0;0;0m\x1b[38;2;211;211;211m  \xE2\x94\x82 "
                             "\x1b[38;2;255;255;255m       \x1b[48;2;63;15;12m         "
                             "\x1b[?25l") == 0);

    // Empty destination renders nothing.
    CHECK(!edit_tbuf_render(&tb, pt(0, 0), rect(5, 5, 5, 5), true, &fb, &xmax));
    CHECK(!edit_tbuf_render(NULL, pt(0, 0), rect(0, 0, 5, 5), true, &fb, NULL));
    CHECK(!edit_tbuf_render(&tb, pt(0, 0), rect(0, 0, 5, 5), true, NULL, NULL));

    edit_tbuf_destroy(&tb);
    edit_fb_destroy(&fb);
    printf("test_trender: %d checks passed\n", checks);
    return 0;
}
