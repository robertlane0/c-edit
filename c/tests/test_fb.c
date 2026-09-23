#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "edit/fb.h"
#include "edit/helpers.h"

static int checks = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #cond);                                     \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static int expect_render(edit_fb_t *f, const char *want) {
    const char *got = NULL;
    size_t n = edit_fb_render(f, &got);
    size_t wlen = strlen(want);
    ++checks;
    if (n != wlen || got == NULL || memcmp(got, want, n) != 0) {
        fprintf(stderr, "FAIL %d: render len %zu != %zu\n", __LINE__, n, wlen);
        if (got != NULL) {
            fprintf(stderr, "got: %.*s\n", (int)(n > 200 ? 200 : n), got);
        }
        fprintf(stderr, "want: %s\n", want);
        return 1;
    }
    return 0;
}

static edit_size_t sz(int w, int h) {
    edit_size_t s = {w, h};
    return s;
}

static edit_rect_t rect(int l, int t, int r, int b) {
    edit_rect_t rc = {l, t, r, b};
    return rc;
}

static edit_point_t pt(int x, int y) {
    edit_point_t p = {x, y};
    return p;
}

int main(void) {
    edit_fb_t fb;
    CHECK(edit_fb_init(&fb) == 0);

    // Vectors from Rust framebuffer (10x4).
    CHECK(edit_fb_flip(&fb, sz(10, 4)) == 0);
    CHECK(edit_fb_replace_text(&fb, 0, 0, 10, "hello", 5) == 0);
    // "wörld→": ö=2B, →=3B.
    CHECK(edit_fb_replace_text(&fb, 1, 0, 10, "w\xC3\xB6rld\xE2\x86\x92", 9) == 0);
    CHECK(expect_render(
              &fb, "\x1b[m\x1b[1;1H\x1b[48;2;0;0;0m\x1b[38;2;255;255;255mhello     "
                   "\x1b[2;1Hw\xC3\xB6rld\xE2\x86\x92    \x1b[3;1H          \x1b[4;1H          "
                   "\x1b[?25l") == 0);

    // Reflip to blank: full redraw of empty lines.
    CHECK(edit_fb_flip(&fb, sz(10, 4)) == 0);
    CHECK(expect_render(&fb, "\x1b[m\x1b[1;1H\x1b[48;2;0;0;0m\x1b[38;2;255;255;255m          "
                             "\x1b[2;1H          \x1b[?25l") == 0);

    // Colors, underline, cursor.
    CHECK(edit_fb_flip(&fb, sz(10, 4)) == 0);
    CHECK(edit_fb_replace_text(&fb, 0, 0, 10, "hi", 2) == 0);
    edit_fb_blend_bg(&fb, rect(0, 0, 10, 1), 0xFFFF0000);
    edit_fb_blend_fg(&fb, rect(0, 0, 2, 1), 0xFF00FF00);
    edit_fb_replace_attr(&fb, rect(0, 0, 2, 1), EDIT_FB_ATTR_ALL, EDIT_FB_ATTR_UNDERLINED);
    edit_fb_set_cursor(&fb, pt(1, 0), false);
    CHECK(expect_render(&fb,
                        "\x1b[m\x1b[1;1H\x1b[48;2;0;0;255m\x1b[38;2;0;255;0m\x1b[4mhi"
                        "\x1b[38;2;255;255;255m\x1b[24m        \x1b[1;2H\x1b[5 q\x1b[?25h") == 0);

    // Scrollbar.
    CHECK(edit_fb_flip(&fb, sz(10, 4)) == 0);
    CHECK(edit_fb_scrollbar(&fb, rect(9, 0, 10, 4), rect(9, 0, 10, 4), 0, 100) == 1);
    CHECK(expect_render(&fb,
                        "\x1b[m\x1b[1;1H\x1b[48;2;0;0;0m\x1b[38;2;255;255;255m         "
                        "\x1b[48;2;128;128;128m\xE2\x96\x88"
                        "\x1b[2;1H\x1b[48;2;0;0;0m         \x1b[48;2;128;128;128m "
                        "\x1b[3;1H\x1b[48;2;0;0;0m         \x1b[48;2;128;128;128m "
                        "\x1b[4;1H\x1b[48;2;0;0;0m         \x1b[48;2;128;128;128m \x1b[?25l") == 0);

    // Palette helpers.
    CHECK(edit_fb_contrasted(&fb, 0xFF808080) == 0xFF000000);
    CHECK(edit_fb_indexed_alpha(&fb, EDIT_FB_RED, 1, 2) == 0x7F10165F);
    CHECK(edit_fb_indexed(&fb, EDIT_FB_BLUE) == 0xFFBE4D20);

    // Reverse.
    CHECK(edit_fb_flip(&fb, sz(10, 4)) == 0);
    CHECK(edit_fb_replace_text(&fb, 0, 0, 10, "reverse me", 10) == 0);
    edit_fb_reverse(&fb, rect(0, 0, 5, 1));
    CHECK(expect_render(&fb,
                        "\x1b[m\x1b[1;1H\x1b[48;2;255;255;255m\x1b[38;2;0;0;0mrever"
                        "\x1b[48;2;0;0;0m\x1b[38;2;255;255;255mse me"
                        "\x1b[2;1H          \x1b[3;1H          \x1b[4;1H          \x1b[?25l") == 0);

    // Theme switch + light-theme auto-color swap path.
    {
        uint32_t light[EDIT_FB_COLORS];
        memcpy(light, EDIT_FB_DEFAULT_THEME, sizeof light);
        light[EDIT_FB_BLACK] = 0xFFFFFFFF;
        light[EDIT_FB_BRIGHT_WHITE] = 0xFF000000;
        edit_fb_set_theme(&fb, light);
        // White is now "dark-looking"? is_dark(white)=false -> swap back.
        CHECK(edit_fb_indexed(&fb, EDIT_FB_BLACK) == 0xFFFFFFFF);
    }

    // NULL safety.
    CHECK(edit_fb_init(NULL) != 0);
    edit_fb_destroy(NULL);
    edit_fb_set_theme(NULL, NULL);
    CHECK(edit_fb_flip(NULL, sz(1, 1)) != 0);
    CHECK(edit_fb_replace_text(NULL, 0, 0, 1, "x", 1) != 0);
    CHECK(edit_fb_scrollbar(NULL, rect(0, 0, 1, 1), rect(0, 0, 1, 1), 0, 1) == 0);
    CHECK(edit_fb_indexed(NULL, EDIT_FB_RED) == 0);
    CHECK(edit_fb_indexed_alpha(NULL, EDIT_FB_RED, 1, 2) == 0);
    CHECK(edit_fb_contrasted(NULL, 1) == 0);
    edit_fb_blend_bg(NULL, rect(0, 0, 1, 1), 1);
    edit_fb_blend_fg(NULL, rect(0, 0, 1, 1), 1);
    edit_fb_reverse(NULL, rect(0, 0, 1, 1));
    edit_fb_replace_attr(NULL, rect(0, 0, 1, 1), 0, 0);
    edit_fb_set_cursor(NULL, pt(0, 0), false);
    CHECK(edit_fb_render(NULL, NULL) == 0);

    edit_fb_destroy(&fb);
    printf("test_fb: %d checks passed\n", checks);
    return 0;
}
