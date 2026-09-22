#include "edit/vt.h"

#include <string.h>

#include "edit/simd.h"

void edit_vt_init(edit_vt_parser_t *p) {
    if (p == NULL) {
        return;
    }
    p->state = EDIT_VTS_GROUND;
    memset(&p->csi, 0, sizeof p->csi);
}

uint64_t edit_vt_timeout_ms(const edit_vt_parser_t *p) {
    if (p != NULL && p->state == EDIT_VTS_ESC) {
        return 50;
    }
    return UINT64_MAX;
}

void edit_vt_parse(edit_vt_parser_t *p, const uint8_t *input, size_t len, edit_vt_stream_t *s) {
    if (s == NULL) {
        return;
    }
    s->parser = p;
    s->input = (len > 0 && input == NULL) ? NULL : input;
    s->len = (s->input == NULL) ? 0 : len;
    s->off = 0;
}

size_t edit_vt_read(edit_vt_stream_t *s, uint8_t *dst, size_t cap) {
    if (s == NULL || s->input == NULL) {
        return 0;
    }
    size_t off = s->off < s->len ? s->off : s->len;
    size_t n = cap < s->len - off ? cap : s->len - off;
    if (n > 0) {
        if (dst == NULL) {
            return 0;
        }
        memcpy(dst, s->input + off, n);
    }
    s->off = off + n;
    return n;
}

static bool is_digit(uint8_t c) {
    return c >= '0' && c <= '9';
}

static bool is_text_byte(uint8_t c) {
    return c >= 0x20 && c != 0x7F;
}

bool edit_vt_next(edit_vt_stream_t *s, edit_token_t *out) {
    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof *out);
    if (s == NULL || s->parser == NULL || s->input == NULL) {
        return false;
    }
    edit_vt_parser_t *parser = s->parser;
    const uint8_t *bytes = s->input;
    size_t n = s->len;

    // Trailing lone ESC resolved by timeout-then-empty-input.
    if (n == 0 && parser->state == EDIT_VTS_ESC) {
        parser->state = EDIT_VTS_GROUND;
        out->kind = EDIT_VT_ESC;
        out->ch = 0;
        return true;
    }

    while (s->off < n) {
        switch (parser->state) {
        case EDIT_VTS_GROUND: {
            uint8_t c = bytes[s->off];
            if (c == 0x1B) {
                parser->state = EDIT_VTS_ESC;
                s->off += 1;
            } else if (c < 0x20 || c == 0x7F) {
                s->off += 1;
                out->kind = EDIT_VT_CTRL;
                out->ch = c;
                return true;
            } else {
                size_t beg = s->off;
                do {
                    s->off += 1;
                } while (s->off < n && is_text_byte(bytes[s->off]));
                out->kind = EDIT_VT_TEXT;
                out->ptr = bytes + beg;
                out->len = s->off - beg;
                return true;
            }
            break;
        }
        case EDIT_VTS_ESC: {
            uint8_t c = bytes[s->off];
            s->off += 1;
            if (c == '[') {
                parser->state = EDIT_VTS_CSI;
                parser->csi.private_byte = 0;
                parser->csi.final_byte = 0;
                while (parser->csi.param_count > 0) {
                    parser->csi.param_count -= 1;
                    parser->csi.params[parser->csi.param_count] = 0;
                }
            } else if (c == ']') {
                parser->state = EDIT_VTS_OSC;
            } else if (c == 'O') {
                parser->state = EDIT_VTS_SS3;
            } else if (c == 'P') {
                parser->state = EDIT_VTS_DCS;
            } else {
                parser->state = EDIT_VTS_GROUND;
                out->kind = EDIT_VT_ESC;
                out->ch = c;
                return true;
            }
            break;
        }
        case EDIT_VTS_SS3: {
            parser->state = EDIT_VTS_GROUND;
            uint8_t c = bytes[s->off];
            s->off += 1;
            out->kind = EDIT_VT_SS3;
            out->ch = c;
            return true;
        }
        case EDIT_VTS_CSI: {
            for (;;) {
                if (parser->csi.param_count < 32) {
                    uint16_t *dst = &parser->csi.params[parser->csi.param_count];
                    while (s->off < n && is_digit(bytes[s->off])) {
                        uint32_t add = (uint32_t)bytes[s->off] - (uint32_t)'0';
                        uint32_t v = (uint32_t)*dst * 10 + add;
                        *dst = v > UINT16_MAX ? UINT16_MAX : (uint16_t)v;
                        s->off += 1;
                    }
                } else {
                    while (s->off < n && is_digit(bytes[s->off])) {
                        s->off += 1;
                    }
                }
                if (s->off >= n) {
                    return false;
                }
                uint8_t c = bytes[s->off];
                s->off += 1;
                if (c >= 0x40 && c <= 0x7E) {
                    parser->state = EDIT_VTS_GROUND;
                    parser->csi.final_byte = c;
                    if (parser->csi.param_count != 0 || parser->csi.params[0] != 0) {
                        parser->csi.param_count += 1;
                    }
                    out->kind = EDIT_VT_CSI;
                    out->csi = parser->csi;
                    return true;
                } else if (c == ';') {
                    parser->csi.param_count += 1;
                } else if (c >= '<' && c <= '?') {
                    parser->csi.private_byte = c;
                }
            }
            break;
        }
        case EDIT_VTS_OSC:
        case EDIT_VTS_DCS: {
            size_t beg = s->off;
            const uint8_t *data = NULL;
            size_t data_len = 0;
            bool partial = false;
            for (;;) {
                s->off = edit_memchr2('\x07', '\x1B', bytes, n, s->off);
                data = bytes + beg;
                data_len = s->off - beg;
                partial = s->off >= n;
                if (partial) {
                    break;
                }
                uint8_t c = bytes[s->off];
                s->off += 1;
                if (c == 0x1B) {
                    if (s->off >= n) {
                        parser->state =
                            parser->state == EDIT_VTS_OSC ? EDIT_VTS_OSCESC : EDIT_VTS_DCSESC;
                        partial = true;
                        break;
                    }
                    if (bytes[s->off] != '\\') {
                        continue;
                    }
                    s->off += 1;
                }
                break;
            }
            // Kind follows the post-loop state: a trailing ESC flips Osc to
            // OscEsc, which reports as Dcs (mirrors the Rust quirk).
            bool emit_osc = parser->state == EDIT_VTS_OSC;
            if (!partial) {
                parser->state = EDIT_VTS_GROUND;
            }
            out->kind = emit_osc ? EDIT_VT_OSC : EDIT_VT_DCS;
            out->ptr = data;
            out->len = data_len;
            out->partial = partial;
            return true;
        }
        case EDIT_VTS_OSCESC:
        case EDIT_VTS_DCSESC: {
            bool was_osc = parser->state == EDIT_VTS_OSCESC;
            if (s->off >= n) {
                return false; // need more input (Rust indexes here and panics)
            }
            if (bytes[s->off] == '\\') {
                parser->state = EDIT_VTS_GROUND;
                s->off += 1;
                out->kind = was_osc ? EDIT_VT_OSC : EDIT_VT_DCS;
                out->ptr = bytes + s->off; // empty (points past terminator)
                out->len = 0;
                out->partial = false;
                return true;
            }
            parser->state = was_osc ? EDIT_VTS_OSC : EDIT_VTS_DCS;
            out->kind = was_osc ? EDIT_VT_OSC : EDIT_VT_DCS;
            // The ESC lived in the previous chunk; emit it standalone.
            static const uint8_t esc = 0x1B;
            out->ptr = &esc;
            out->len = 1;
            out->partial = true;
            return true;
        }
        }
    }
    return false;
}
