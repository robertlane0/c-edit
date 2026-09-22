#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "edit/vt.h"

static int checks = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #cond);                                     \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

typedef struct {
    edit_vt_kind_t kind;
    const char *bytes; // TEXT/OSC/DCS payload (NULL checks only kind/ch)
    size_t len;
    unsigned ch;
    bool partial;
    const int *params; // -1 terminated CSI expectations
    unsigned priv;
    unsigned fin;
} expect_t;

static int drain(edit_vt_parser_t *p, const char *input, const expect_t *want, size_t nwant) {
    edit_vt_stream_t s;
    edit_vt_parse(p, (const uint8_t *)input, strlen(input), &s);
    for (size_t i = 0; i < nwant; ++i) {
        edit_token_t t;
        ++checks;
        if (!edit_vt_next(&s, &t)) {
            fprintf(stderr, "FAIL %d: stream ended at token %zu (%s)\n", __LINE__, i, input);
            return 1;
        }
        ++checks;
        if (t.kind != want[i].kind) {
            fprintf(stderr, "FAIL %d: token %zu kind %d != %d (%s)\n", __LINE__, i, (int)t.kind,
                    (int)want[i].kind, input);
            return 1;
        }
        if (want[i].kind == EDIT_VT_TEXT || want[i].kind == EDIT_VT_OSC ||
            want[i].kind == EDIT_VT_DCS) {
            ++checks;
            if (t.len != want[i].len || memcmp(t.ptr, want[i].bytes, t.len) != 0 ||
                t.partial != want[i].partial) {
                fprintf(stderr, "FAIL %d: token %zu payload mismatch (%s)\n", __LINE__, i, input);
                return 1;
            }
        }
        if (want[i].kind == EDIT_VT_CTRL || want[i].kind == EDIT_VT_ESC ||
            want[i].kind == EDIT_VT_SS3) {
            ++checks;
            if (t.ch != want[i].ch) {
                fprintf(stderr, "FAIL %d: token %zu ch %u != %u (%s)\n", __LINE__, i, t.ch,
                        want[i].ch, input);
                return 1;
            }
        }
        if (want[i].kind == EDIT_VT_CSI) {
            const int *xp = want[i].params;
            size_t xn = 0;
            while (xp[xn] >= 0) {
                ++xn;
            }
            ++checks;
            if (t.csi.param_count != xn || t.csi.private_byte != want[i].priv ||
                t.csi.final_byte != want[i].fin) {
                fprintf(stderr, "FAIL %d: token %zu csi count %zu/%zu priv %u/%u fin %u/%u\n",
                        __LINE__, i, t.csi.param_count, xn, t.csi.private_byte, want[i].priv,
                        t.csi.final_byte, want[i].fin);
                return 1;
            }
            for (size_t k = 0; k < xn && k < 32; ++k) {
                ++checks;
                if (t.csi.params[k] != (unsigned)xp[k]) {
                    fprintf(stderr, "FAIL %d: param %zu mismatch\n", __LINE__, k);
                    return 1;
                }
            }
        }
    }
    edit_token_t t;
    ++checks;
    if (edit_vt_next(&s, &t)) {
        fprintf(stderr, "FAIL %d: trailing token (%s)\n", __LINE__, input);
        return 1;
    }
    return 0;
}

#define T(b) {EDIT_VT_TEXT, b, sizeof b - 1, 0, false, NULL, 0, 0}
#define C(c) {EDIT_VT_CTRL, NULL, 0, c, false, NULL, 0, 0}
#define E(c) {EDIT_VT_ESC, NULL, 0, c, false, NULL, 0, 0}
#define S3(c) {EDIT_VT_SS3, NULL, 0, c, false, NULL, 0, 0}
#define CSI(priv, fin, ...)                                                                        \
    {EDIT_VT_CSI, NULL, 0, 0, false, ((const int[]){__VA_ARGS__, -1}), priv, fin}
#define CSI0(priv, fin) {EDIT_VT_CSI, NULL, 0, 0, false, ((const int[]){-1}), priv, fin}
#define O(b, part) {EDIT_VT_OSC, b, sizeof b - 1, 0, part, NULL, 0, 0}
#define D(b, part) {EDIT_VT_DCS, b, sizeof b - 1, 0, part, NULL, 0, 0}

int main(void) {
    edit_vt_parser_t p;
    edit_vt_init(&p);
    CHECK(edit_vt_timeout_ms(&p) == UINT64_MAX);

    // Vectors from Rust vt.rs.
    CHECK(drain(&p, "hello", ((const expect_t[]){T("hello")}), 1) == 0);
    CHECK(drain(&p,
                "a\x07"
                "b\x7f",
                ((const expect_t[]){T("a"), C(7), T("b"), C(0x7f)}), 4) == 0);
    CHECK(drain(&p, "\x1bX", ((const expect_t[]){E('X')}), 1) == 0);
    CHECK(drain(&p, "\x1b[1;2H", ((const expect_t[]){CSI(0, 'H', 1, 2)}), 1) == 0);
    CHECK(drain(&p, "\x1b[m", ((const expect_t[]){CSI0(0, 'm')}), 1) == 0);
    CHECK(drain(&p, "\x1b[?25l", ((const expect_t[]){CSI('?', 'l', 25)}), 1) == 0);
    CHECK(drain(&p, "\x1b[99999999999m", ((const expect_t[]){CSI(0, 'm', 65535)}), 1) == 0);
    CHECK(drain(&p, "\x1b[1 $q", ((const expect_t[]){CSI(0, 'q', 1)}), 1) == 0);
    CHECK(drain(&p, "\x1bOP", ((const expect_t[]){S3('P')}), 1) == 0);
    CHECK(drain(&p,
                "\x1b]0;title\x07"
                "after",
                ((const expect_t[]){O("0;title", false), T("after")}), 2) == 0);
    CHECK(drain(&p, "\x1b]0;title\x1b\\after",
                ((const expect_t[]){O("0;title", false), T("after")}), 2) == 0);
    CHECK(drain(&p, "\x1bPdata\x1b\\after", ((const expect_t[]){D("data", false), T("after")}),
                2) == 0);
    CHECK(drain(&p, "\x1b]0;ti", ((const expect_t[]){O("0;ti", true)}), 1) == 0); // partial
    edit_vt_init(&p);
    CHECK(drain(&p, "\x1b]0;ti\x1b", ((const expect_t[]){D("0;ti", true)}), 1) == 0); // DCS quirk
    edit_vt_init(&p);
    CHECK(drain(&p, "\r\n", ((const expect_t[]){C('\r'), C('\n')}), 2) == 0);
    CHECK(drain(&p, "h\xC3\xA9llo\xE2\x86\x92", ((const expect_t[]){T("h\xC3\xA9llo\xE2\x86\x92")}),
                1) == 0);

    // 34 params: first 32 kept, count runs on.
    {
        static const int p34[] = {1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12,
                                  13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
                                  25, 26, 27, 28, 29, 30, 31, 32, 33, 34, -1};
        expect_t w = {EDIT_VT_CSI, NULL, 0, 0, false, NULL, 0, 'm'};
        w.params = p34;
        CHECK(drain(&p,
                    "\x1b[1;2;3;4;5;6;7;8;9;10;11;12;13;14;15;16;17;18;19;20;21;22;23;24;25;26;27;"
                    "28;29;30;31;32;33;34m",
                    &w, 1) == 0);
    }

    // Multi-chunk sequences with a persistent parser.
    edit_vt_init(&p);
    CHECK(drain(&p, "\x1b]0;ti", ((const expect_t[]){O("0;ti", true)}), 1) == 0);
    CHECK(drain(&p,
                "tle\x07"
                "after",
                ((const expect_t[]){O("tle", false), T("after")}), 2) == 0);
    edit_vt_init(&p);
    CHECK(drain(&p, "\x1b]0;ti\x1b", ((const expect_t[]){D("0;ti", true)}), 1) == 0);
    CHECK(drain(&p, "\\after", ((const expect_t[]){O("", false), T("after")}), 2) == 0);
    edit_vt_init(&p);
    CHECK(drain(&p, "\x1b]0;ti\x1b", ((const expect_t[]){D("0;ti", true)}), 1) == 0);
    CHECK(drain(&p, "x", ((const expect_t[]){O("\x1b", true), O("x", true)}), 2) == 0);
    edit_vt_init(&p);
    CHECK(drain(&p, "\x1b[1;", NULL, 0) == 0);
    CHECK(drain(&p, "2H", ((const expect_t[]){CSI(0, 'H', 1, 2)}), 1) == 0);
    edit_vt_init(&p);
    CHECK(drain(&p, "\x1b", NULL, 0) == 0);
    CHECK(edit_vt_timeout_ms(&p) == 50);
    CHECK(drain(&p, "X", ((const expect_t[]){E('X')}), 1) == 0);
    // Lone ESC resolved by timeout-then-empty.
    edit_vt_init(&p);
    CHECK(drain(&p, "\x1b", NULL, 0) == 0);
    CHECK(drain(&p, "", ((const expect_t[]){E(0)}), 1) == 0);
    CHECK(edit_vt_timeout_ms(&p) == UINT64_MAX);
    edit_vt_init(&p);
    CHECK(drain(&p, "\x1bPda", ((const expect_t[]){D("da", true)}), 1) == 0);
    CHECK(drain(&p, "ta\x1b\\", ((const expect_t[]){D("ta", false)}), 1) == 0);

    // Stream read copies raw bytes.
    {
        edit_vt_stream_t s;
        edit_vt_parse(&p, (const uint8_t *)"hello", 5, &s);
        uint8_t buf[3];
        CHECK(edit_vt_read(&s, buf, sizeof buf) == 3);
        CHECK(memcmp(buf, "hel", 3) == 0);
        edit_token_t t;
        CHECK(edit_vt_next(&s, &t) && t.kind == EDIT_VT_TEXT && t.len == 2);
        CHECK(!edit_vt_next(&s, NULL));
        CHECK(edit_vt_read(NULL, buf, 3) == 0);
    }

    // NULL safety.
    edit_vt_init(NULL);
    CHECK(edit_vt_timeout_ms(NULL) == UINT64_MAX);
    edit_vt_parse(NULL, NULL, 0, NULL);
    CHECK(!edit_vt_next(NULL, NULL));

    printf("test_vt: %d checks passed\n", checks);
    return 0;
}
