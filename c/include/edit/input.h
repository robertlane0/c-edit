#ifndef EDIT_INPUT_H
#define EDIT_INPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "edit/helpers.h"
#include "edit/vt.h"

#ifdef __cplusplus
extern "C" {
#endif

// VT key/modifier codes matching Windows VK_* (cf. Rust input::vk/kbmod).
typedef uint32_t edit_key_t;
typedef uint32_t edit_keymod_t;

#define EDIT_VK_NULL 0x00
#define EDIT_VK_BACK 0x08
#define EDIT_VK_TAB 0x09
#define EDIT_VK_RETURN 0x0D
#define EDIT_VK_ESCAPE 0x1B
#define EDIT_VK_SPACE 0x20
#define EDIT_VK_PRIOR 0x21
#define EDIT_VK_NEXT 0x22
#define EDIT_VK_END 0x23
#define EDIT_VK_HOME 0x24
#define EDIT_VK_LEFT 0x25
#define EDIT_VK_UP 0x26
#define EDIT_VK_RIGHT 0x27
#define EDIT_VK_DOWN 0x28
#define EDIT_VK_INSERT 0x2D
#define EDIT_VK_DELETE 0x2E
#define EDIT_VK_0 '0'
#define EDIT_VK_1 '1'
#define EDIT_VK_2 '2'
#define EDIT_VK_3 '3'
#define EDIT_VK_4 '4'
#define EDIT_VK_5 '5'
#define EDIT_VK_6 '6'
#define EDIT_VK_7 '7'
#define EDIT_VK_8 '8'
#define EDIT_VK_9 '9'
#define EDIT_VK_A 'A'
#define EDIT_VK_B 'B'
#define EDIT_VK_C 'C'
#define EDIT_VK_D 'D'
#define EDIT_VK_E 'E'
#define EDIT_VK_F 'F'
#define EDIT_VK_G 'G'
#define EDIT_VK_H 'H'
#define EDIT_VK_I 'I'
#define EDIT_VK_J 'J'
#define EDIT_VK_K 'K'
#define EDIT_VK_L 'L'
#define EDIT_VK_M 'M'
#define EDIT_VK_N 'N'
#define EDIT_VK_O 'O'
#define EDIT_VK_P 'P'
#define EDIT_VK_Q 'Q'
#define EDIT_VK_R 'R'
#define EDIT_VK_S 'S'
#define EDIT_VK_T 'T'
#define EDIT_VK_U 'U'
#define EDIT_VK_V 'V'
#define EDIT_VK_W 'W'
#define EDIT_VK_X 'X'
#define EDIT_VK_Y 'Y'
#define EDIT_VK_Z 'Z'
#define EDIT_VK_F1 0x70
#define EDIT_VK_F2 0x71
#define EDIT_VK_F3 0x72
#define EDIT_VK_F4 0x73
#define EDIT_VK_F5 0x74
#define EDIT_VK_F6 0x75
#define EDIT_VK_F7 0x76
#define EDIT_VK_F8 0x77
#define EDIT_VK_F9 0x78
#define EDIT_VK_F10 0x79
#define EDIT_VK_F11 0x7A
#define EDIT_VK_F12 0x7B
#define EDIT_VK_F13 0x7C
#define EDIT_VK_F14 0x7D
#define EDIT_VK_F15 0x7E
#define EDIT_VK_F16 0x7F
#define EDIT_VK_F17 0x80
#define EDIT_VK_F18 0x81
#define EDIT_VK_F19 0x82
#define EDIT_VK_F20 0x83
#define EDIT_VK_F21 0x84
#define EDIT_VK_F22 0x85
#define EDIT_VK_F23 0x86
#define EDIT_VK_F24 0x87

#define EDIT_KBMOD_NONE 0x00000000
#define EDIT_KBMOD_CTRL 0x01000000
#define EDIT_KBMOD_ALT 0x02000000
#define EDIT_KBMOD_SHIFT 0x04000000

// Key helpers (cf. Rust InputKey).
bool edit_key_from_ascii(uint32_t ch, edit_key_t *out);
uint32_t edit_key_value(edit_key_t key);
edit_key_t edit_key_code(edit_key_t key);
edit_keymod_t edit_key_modifiers(edit_key_t key);
bool edit_key_has_modifier(edit_key_t key, edit_keymod_t mod);
edit_key_t edit_key_with_modifiers(edit_key_t key, edit_keymod_t mod);
bool edit_mod_contains(edit_keymod_t mods, edit_keymod_t mod);

typedef enum {
    EDIT_IN_RESIZE,
    EDIT_IN_TEXT,
    EDIT_IN_KEYBOARD,
    EDIT_IN_MOUSE,
} edit_in_kind_t;

typedef enum {
    EDIT_MOUSE_NONE,
    EDIT_MOUSE_LEFT,
    EDIT_MOUSE_MIDDLE,
    EDIT_MOUSE_RIGHT,
    EDIT_MOUSE_RELEASE,
    EDIT_MOUSE_SCROLL,
} edit_mouse_state_t;

typedef struct {
    const uint8_t *ptr;
    size_t len;
    bool bracketed;
} edit_in_text_t;

typedef struct {
    edit_mouse_state_t state;
    edit_keymod_t modifiers;
    edit_point_t position;
    edit_point_t scroll;
} edit_in_mouse_t;

typedef struct {
    edit_in_kind_t kind;
    edit_size_t size;      // RESIZE
    edit_in_text_t text;   // TEXT
    edit_key_t key;        // KEYBOARD
    edit_in_mouse_t mouse; // MOUSE
} edit_input_t;

typedef struct {
    bool bracketed_paste;
    bool x10_want;
    uint8_t x10_buf[3];
    size_t x10_len;
} edit_in_parser_t;

typedef struct {
    edit_in_parser_t *parser;
    edit_vt_stream_t stream;
} edit_in_stream_t;

void edit_in_init(edit_in_parser_t *p);
void edit_in_parse(edit_in_parser_t *p, const edit_vt_stream_t *vts, edit_in_stream_t *s);
bool edit_in_next(edit_in_stream_t *s, edit_input_t *out);

#ifdef __cplusplus
}
#endif

#endif
