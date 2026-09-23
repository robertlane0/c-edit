#include "edit/input.h"

#include <string.h>

bool edit_key_from_ascii(uint32_t ch, edit_key_t *out) {
    if (out == NULL) {
        return false;
    }
    if (ch == ' ' || (ch >= '0' && ch <= '9')) {
        *out = ch;
        return true;
    }
    if (ch >= 'a' && ch <= 'z') {
        *out = ch & ~0x20U;
        return true;
    }
    if (ch >= 'A' && ch <= 'Z') {
        *out = (uint32_t)EDIT_KBMOD_SHIFT | ch;
        return true;
    }
    return false;
}

uint32_t edit_key_value(edit_key_t key) {
    return key;
}

edit_key_t edit_key_code(edit_key_t key) {
    return key & 0x00FFFFFFU;
}

edit_keymod_t edit_key_modifiers(edit_key_t key) {
    return key & 0xFF000000U;
}

bool edit_key_has_modifier(edit_key_t key, edit_keymod_t mod) {
    return (key & mod) != 0;
}

edit_key_t edit_key_with_modifiers(edit_key_t key, edit_keymod_t mod) {
    return key | mod;
}

bool edit_mod_contains(edit_keymod_t mods, edit_keymod_t mod) {
    return (mods & mod) != 0;
}

void edit_in_init(edit_in_parser_t *p) {
    if (p == NULL) {
        return;
    }
    memset(p, 0, sizeof *p);
}

void edit_in_parse(edit_in_parser_t *p, const edit_vt_stream_t *vts, edit_in_stream_t *s) {
    if (s == NULL) {
        return;
    }
    s->parser = p;
    memset(&s->stream, 0, sizeof s->stream);
    if (vts != NULL) {
        s->stream = *vts;
    }
}

static edit_keymod_t parse_modifiers(const edit_csi_t *csi) {
    edit_keymod_t mods = EDIT_KBMOD_NONE;
    uint16_t p1 = csi->param_count > 1 ? csi->params[1] : 0;
    uint16_t v = p1 > 0 ? (uint16_t)(p1 - 1) : 0;
    if ((v & 0x01) != 0) {
        mods |= EDIT_KBMOD_SHIFT;
    }
    if ((v & 0x02) != 0) {
        mods |= EDIT_KBMOD_ALT;
    }
    if ((v & 0x04) != 0) {
        mods |= EDIT_KBMOD_CTRL;
    }
    return mods;
}

static bool handle_bracketed_paste(edit_in_stream_t *s, edit_input_t *out) {
    size_t beg = s->stream.off;
    size_t end = beg;
    edit_token_t tok;
    while (edit_vt_next(&s->stream, &tok)) {
        if (tok.kind == EDIT_VT_CSI && tok.csi.final_byte == '~' && tok.csi.param_count > 0 &&
            tok.csi.params[0] == 201) {
            s->parser->bracketed_paste = false;
            break;
        }
        end = s->stream.off;
    }
    if (end == beg) {
        return false;
    }
    out->kind = EDIT_IN_TEXT;
    out->text.ptr = s->stream.input + beg;
    out->text.len = end - beg;
    out->text.bracketed = true;
    return true;
}

static bool parse_x10(edit_in_stream_t *s, edit_input_t *out) {
    edit_in_parser_t *p = s->parser;
    uint8_t *dst = p->x10_buf + p->x10_len;
    size_t got = edit_vt_read(&s->stream, dst, 3 - p->x10_len);
    p->x10_len += got;
    if (p->x10_len < 3) {
        return false;
    }
    uint8_t button = p->x10_buf[0] & 0x03;
    uint8_t modifier = p->x10_buf[0] & 0x1C;
    int32_t x = (int32_t)p->x10_buf[1] - 0x21;
    int32_t y = (int32_t)p->x10_buf[2] - 0x21;
    edit_mouse_state_t state = EDIT_MOUSE_NONE;
    if (button == 0) {
        state = EDIT_MOUSE_LEFT;
    } else if (button == 1) {
        state = EDIT_MOUSE_MIDDLE;
    } else if (button == 2) {
        state = EDIT_MOUSE_RIGHT;
    }
    edit_keymod_t mods = EDIT_KBMOD_NONE;
    if (modifier == 4) {
        mods = EDIT_KBMOD_SHIFT;
    } else if (modifier == 8) {
        mods = EDIT_KBMOD_ALT;
    } else if (modifier == 16) {
        mods = EDIT_KBMOD_CTRL;
    }
    p->x10_want = false;
    p->x10_len = 0;
    out->kind = EDIT_IN_MOUSE;
    out->mouse.state = state;
    out->mouse.modifiers = mods;
    out->mouse.position.x = x;
    out->mouse.position.y = y;
    out->mouse.scroll.x = 0;
    out->mouse.scroll.y = 0;
    return true;
}

bool edit_in_next(edit_in_stream_t *s, edit_input_t *out) {
    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof *out);
    if (s == NULL || s->parser == NULL) {
        return false;
    }
    for (;;) {
        if (s->parser->bracketed_paste) {
            if (handle_bracketed_paste(s, out)) {
                return true;
            }
            return false;
        }
        if (s->parser->x10_want) {
            if (parse_x10(s, out)) {
                return true;
            }
            return false;
        }
        edit_token_t tok;
        if (!edit_vt_next(&s->stream, &tok)) {
            return false;
        }
        switch (tok.kind) {
        case EDIT_VT_TEXT:
            out->kind = EDIT_IN_TEXT;
            out->text.ptr = tok.ptr;
            out->text.len = tok.len;
            out->text.bracketed = false;
            return true;
        case EDIT_VT_CTRL: {
            uint32_t ch = tok.ch;
            if (ch == 0 || ch == '\t' || ch == '\r') {
                out->kind = EDIT_IN_KEYBOARD;
                out->key = ch;
                return true;
            }
            if (ch == '\n') {
                out->kind = EDIT_IN_KEYBOARD;
                out->key = EDIT_KBMOD_CTRL | EDIT_VK_RETURN;
                return true;
            }
            if (ch <= 0x1A) {
                out->kind = EDIT_IN_KEYBOARD;
                out->key = EDIT_KBMOD_CTRL | (ch | 0x40U);
                return true;
            }
            if (ch == 0x7F) {
                out->kind = EDIT_IN_KEYBOARD;
                out->key = EDIT_VK_BACK;
                return true;
            }
            break;
        }
        case EDIT_VT_ESC: {
            uint32_t ch = tok.ch;
            if (ch == 0) {
                out->kind = EDIT_IN_KEYBOARD;
                out->key = EDIT_VK_ESCAPE;
                return true;
            }
            if (ch == '\n') {
                out->kind = EDIT_IN_KEYBOARD;
                out->key = (uint32_t)EDIT_KBMOD_CTRL | EDIT_KBMOD_ALT | EDIT_VK_RETURN;
                return true;
            }
            if (ch >= ' ' && ch <= '~') {
                uint32_t key = ch & ~0x20U;
                edit_keymod_t mods =
                    ((ch & 0x20U) != 0) ? EDIT_KBMOD_ALT : EDIT_KBMOD_ALT | EDIT_KBMOD_SHIFT;
                out->kind = EDIT_IN_KEYBOARD;
                out->key = mods | key;
                return true;
            }
            break;
        }
        case EDIT_VT_SS3:
            if (tok.ch >= 'P' && tok.ch <= 'S') {
                out->kind = EDIT_IN_KEYBOARD;
                out->key = (uint32_t)EDIT_VK_F1 + tok.ch - (uint32_t)'P';
                return true;
            }
            break;
        case EDIT_VT_CSI: {
            const edit_csi_t *csi = &tok.csi;
            uint32_t fin = csi->final_byte;
            if (fin >= 'A' && fin <= 'H') {
                static const uint8_t lut[8] = {
                    EDIT_VK_UP, EDIT_VK_DOWN, EDIT_VK_RIGHT, EDIT_VK_LEFT, 0, EDIT_VK_END,
                    0,          EDIT_VK_HOME};
                uint8_t vk = lut[fin - (uint32_t)'A'];
                if (vk != 0) {
                    out->kind = EDIT_IN_KEYBOARD;
                    out->key = (edit_key_t)vk | parse_modifiers(csi);
                    return true;
                }
            } else if (fin == 'Z') {
                out->kind = EDIT_IN_KEYBOARD;
                out->key = EDIT_KBMOD_SHIFT | EDIT_VK_TAB;
                return true;
            } else if (fin == '~') {
                static const uint8_t lut[35] = {
                    0,
                    EDIT_VK_HOME,
                    EDIT_VK_INSERT,
                    EDIT_VK_DELETE,
                    EDIT_VK_END,
                    EDIT_VK_PRIOR,
                    EDIT_VK_NEXT,
                    0,
                    0,
                    0,
                    0,
                    0,
                    0,
                    0,
                    0,
                    EDIT_VK_F5,
                    0,
                    EDIT_VK_F6,
                    EDIT_VK_F7,
                    EDIT_VK_F8,
                    EDIT_VK_F9,
                    EDIT_VK_F10,
                    0,
                    EDIT_VK_F11,
                    EDIT_VK_F12,
                    EDIT_VK_F13,
                    EDIT_VK_F14,
                    0,
                    EDIT_VK_F15,
                    EDIT_VK_F16,
                    0,
                    EDIT_VK_F17,
                    EDIT_VK_F18,
                    EDIT_VK_F19,
                    EDIT_VK_F20,
                };
                uint16_t p0 = csi->param_count > 0 ? csi->params[0] : 0;
                if (p0 < 35) {
                    uint8_t vk = lut[p0];
                    if (vk != 0) {
                        out->kind = EDIT_IN_KEYBOARD;
                        out->key = (edit_key_t)vk | parse_modifiers(csi);
                        return true;
                    }
                } else if (p0 == 200) {
                    s->parser->bracketed_paste = true;
                }
            } else if ((fin == 'm' || fin == 'M') && csi->private_byte == '<') {
                uint16_t btn = csi->param_count > 0 ? csi->params[0] : 0;
                edit_in_mouse_t m;
                memset(&m, 0, sizeof m);
                if ((btn & 0x40) != 0) {
                    m.state = EDIT_MOUSE_SCROLL;
                    m.scroll.y += ((btn & 0x01) != 0) ? 3 : -3;
                } else if (fin == 'M') {
                    static const edit_mouse_state_t states[4] = {
                        EDIT_MOUSE_LEFT,
                        EDIT_MOUSE_MIDDLE,
                        EDIT_MOUSE_RIGHT,
                        EDIT_MOUSE_NONE,
                    };
                    m.state = states[btn & 0x03];
                }
                m.modifiers = EDIT_KBMOD_NONE;
                if ((btn & 0x04) != 0) {
                    m.modifiers |= EDIT_KBMOD_SHIFT;
                }
                if ((btn & 0x08) != 0) {
                    m.modifiers |= EDIT_KBMOD_ALT;
                }
                if ((btn & 0x10F) != 0) {
                    m.modifiers |= EDIT_KBMOD_CTRL;
                }
                uint16_t px = csi->param_count > 1 ? csi->params[1] : 0;
                uint16_t py = csi->param_count > 2 ? csi->params[2] : 0;
                m.position.x = (int32_t)px - 1;
                m.position.y = (int32_t)py - 1;
                out->kind = EDIT_IN_MOUSE;
                out->mouse = m;
                return true;
            } else if (fin == 'M' && csi->param_count == 0) {
                s->parser->x10_want = true;
            } else if (fin == 't' && csi->param_count > 0 && csi->params[0] == 8) {
                uint16_t w = csi->param_count > 2 ? csi->params[2] : 0;
                uint16_t h = csi->param_count > 1 ? csi->params[1] : 0;
                int32_t width = (int32_t)w < 1 ? 1 : (int32_t)w > 32767 ? 32767 : (int32_t)w;
                int32_t height = (int32_t)h < 1 ? 1 : (int32_t)h > 32767 ? 32767 : (int32_t)h;
                out->kind = EDIT_IN_RESIZE;
                out->size.width = width;
                out->size.height = height;
                return true;
            }
            break;
        }
        default:
            break;
        }
    }
}
