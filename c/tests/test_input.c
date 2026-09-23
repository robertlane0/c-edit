#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "edit/input.h"

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
    edit_in_kind_t kind;
    const char *text;
    bool bracketed;
    uint32_t key;
    int mouse_state;
    uint32_t mods;
    int px;
    int py;
    int sx;
    int sy;
    int rw;
    int rh;
} expect_t;

#define T(t) {EDIT_IN_TEXT, t, false, 0, 0, 0, 0, 0, 0, 0, 0, 0}
#define TB(t) {EDIT_IN_TEXT, t, true, 0, 0, 0, 0, 0, 0, 0, 0, 0}
#define K(k) {EDIT_IN_KEYBOARD, NULL, false, k, 0, 0, 0, 0, 0, 0, 0, 0}
#define MOUSE(st, mods, px, py, sx, sy)                                                            \
    {EDIT_IN_MOUSE, NULL, false, 0, st, mods, px, py, sx, sy, 0, 0}
#define R(w, h) {EDIT_IN_RESIZE, NULL, false, 0, 0, 0, 0, 0, 0, 0, w, h}

static int drain(const char *input, const expect_t *want, size_t nwant) {
    edit_vt_parser_t vp;
    edit_vt_init(&vp);
    edit_vt_stream_t vs;
    edit_vt_parse(&vp, (const uint8_t *)input, strlen(input), &vs);
    edit_in_parser_t p;
    edit_in_init(&p);
    edit_in_stream_t s;
    edit_in_parse(&p, &vs, &s);
    for (size_t i = 0; i < nwant; ++i) {
        edit_input_t in;
        ++checks;
        if (!edit_in_next(&s, &in)) {
            fprintf(stderr, "FAIL %d: stream ended at %zu (%s)\n", __LINE__, i, input);
            return 1;
        }
        ++checks;
        if (in.kind != want[i].kind) {
            fprintf(stderr, "FAIL %d: event %zu kind %d != %d\n", __LINE__, i, (int)in.kind,
                    (int)want[i].kind);
            return 1;
        }
        if (in.kind == EDIT_IN_TEXT) {
            ++checks;
            if (in.text.len != strlen(want[i].text) ||
                memcmp(in.text.ptr, want[i].text, in.text.len) != 0 ||
                in.text.bracketed != want[i].bracketed) {
                fprintf(stderr, "FAIL %d: text event %zu mismatch\n", __LINE__, i);
                return 1;
            }
        } else if (in.kind == EDIT_IN_KEYBOARD) {
            ++checks;
            if (in.key != want[i].key) {
                fprintf(stderr, "FAIL %d: key %zu %#x != %#x\n", __LINE__, i, in.key, want[i].key);
                return 1;
            }
        } else if (in.kind == EDIT_IN_MOUSE) {
            ++checks;
            if ((int)in.mouse.state != want[i].mouse_state || in.mouse.modifiers != want[i].mods ||
                in.mouse.position.x != want[i].px || in.mouse.position.y != want[i].py ||
                in.mouse.scroll.x != want[i].sx || in.mouse.scroll.y != want[i].sy) {
                fprintf(stderr, "FAIL %d: mouse event %zu mismatch\n", __LINE__, i);
                return 1;
            }
        } else if (in.kind == EDIT_IN_RESIZE) {
            ++checks;
            if (in.size.width != want[i].rw || in.size.height != want[i].rh) {
                fprintf(stderr, "FAIL %d: resize event %zu mismatch\n", __LINE__, i);
                return 1;
            }
        }
    }
    edit_input_t in;
    ++checks;
    if (edit_in_next(&s, &in)) {
        fprintf(stderr, "FAIL %d: trailing event (%s)\n", __LINE__, input);
        return 1;
    }
    return 0;
}

int main(void) {
    // Vectors from Rust input::Stream.
    CHECK(drain("hi", ((const expect_t[]){T("hi")}), 1) == 0);
    CHECK(drain("\r", ((const expect_t[]){K(0x0D)}), 1) == 0);
    CHECK(drain("\x03", ((const expect_t[]){K(0x1000043)}), 1) == 0);
    CHECK(drain("\x7f", ((const expect_t[]){K(0x08)}), 1) == 0);
    CHECK(drain("\t", ((const expect_t[]){K(0x09)}), 1) == 0);
    CHECK(drain("\x1b", NULL, 0) == 0); // pending ESC: nothing yet
    CHECK(drain("\x1bx", ((const expect_t[]){K(0x2000058)}), 1) == 0);
    CHECK(drain("\x1bX", ((const expect_t[]){K(0x6000058)}), 1) == 0);
    CHECK(drain("\x1b[A", ((const expect_t[]){K(0x26)}), 1) == 0);
    CHECK(drain("\x1b[1;2A", ((const expect_t[]){K(0x4000026)}), 1) == 0);
    CHECK(drain("\x1b[H", ((const expect_t[]){K(0x24)}), 1) == 0);
    CHECK(drain("\x1b[15~", ((const expect_t[]){K(0x74)}), 1) == 0);
    CHECK(drain("\x1b[3~", ((const expect_t[]){K(0x2E)}), 1) == 0);
    CHECK(drain("\x1b[Z", ((const expect_t[]){K(0x4000009)}), 1) == 0);
    CHECK(drain("\x1bOP", ((const expect_t[]){K(0x70)}), 1) == 0);
    CHECK(drain("\x1b[<0;5;3M", ((const expect_t[]){MOUSE(1, 0, 4, 2, 0, 0)}), 1) == 0);
    CHECK(drain("\x1b[<3;5;3m", ((const expect_t[]){MOUSE(0, 0x1000000, 4, 2, 0, 0)}), 1) == 0);
    CHECK(drain("\x1b[<64;5;3M", ((const expect_t[]){MOUSE(5, 0, 4, 2, 0, -3)}), 1) == 0);
    CHECK(drain("\x1b[M !!", ((const expect_t[]){MOUSE(1, 0, 0, 0, 0, 0)}), 1) == 0);
    CHECK(drain("\x1b[200~pasted\x1b[201~after", ((const expect_t[]){TB("pasted"), T("after")}),
                2) == 0);
    CHECK(drain("\x1b[8;24;80t", ((const expect_t[]){R(80, 24)}), 1) == 0);
    CHECK(drain("\x1b\n", ((const expect_t[]){K(0x300000D)}), 1) == 0);

    // Key helpers.
    edit_key_t k = 0;
    CHECK(edit_key_from_ascii('a', &k) && k == 'A');
    CHECK(edit_key_from_ascii('Z', &k) && k == (EDIT_KBMOD_SHIFT | 'Z'));
    CHECK(edit_key_from_ascii('5', &k) && k == '5');
    CHECK(!edit_key_from_ascii('!', NULL) && !edit_key_from_ascii('!', &k));
    CHECK(edit_key_value(EDIT_KBMOD_CTRL | 0x41) == (EDIT_KBMOD_CTRL | 0x41));
    CHECK(edit_key_code(EDIT_KBMOD_CTRL | 0x41) == 0x41);
    CHECK(edit_key_modifiers(EDIT_KBMOD_CTRL | 0x41) == EDIT_KBMOD_CTRL);
    CHECK(edit_key_has_modifier(EDIT_KBMOD_CTRL | 0x41, EDIT_KBMOD_CTRL));
    CHECK(!edit_key_has_modifier(0x41, EDIT_KBMOD_CTRL));
    CHECK(edit_key_with_modifiers(0x41, EDIT_KBMOD_ALT) == (EDIT_KBMOD_ALT | 0x41));
    CHECK(edit_mod_contains(EDIT_KBMOD_CTRL | EDIT_KBMOD_SHIFT, EDIT_KBMOD_SHIFT));

    // NULL safety.
    CHECK(!edit_in_next(NULL, NULL));
    edit_in_init(NULL);
    edit_in_parse(NULL, NULL, NULL);

    printf("test_input: %d checks passed\n", checks);
    return 0;
}
