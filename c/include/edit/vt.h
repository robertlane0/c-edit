#ifndef EDIT_VT_H
#define EDIT_VT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// VT sequence parser (cf. Rust vt.rs). Input is bytes; multibyte sequences
// pass through Text runs untouched (runs never split them).
typedef enum {
    EDIT_VT_TEXT, // plain text run (no control chars)
    EDIT_VT_CTRL, // single C0/C1 control char (ch)
    EDIT_VT_ESC,  // ESC x (ch; '\0' on timeout-then-empty)
    EDIT_VT_SS3,  // ESC O x (ch)
    EDIT_VT_CSI,  // ESC [ ... (csi)
    EDIT_VT_OSC,  // ESC ] ... (ptr/len, partial across chunks)
    EDIT_VT_DCS,  // ESC P ... (ptr/len, partial across chunks)
} edit_vt_kind_t;

typedef struct {
    uint16_t params[32];
    size_t param_count;
    uint32_t private_byte; // 0 if none
    uint32_t final_byte;
} edit_csi_t;

typedef struct {
    edit_vt_kind_t kind;
    const uint8_t *ptr; // TEXT/OSC/DCS bytes (borrow input)
    size_t len;
    uint32_t ch;    // CTRL/ESC/SS3 char
    bool partial;   // OSC/DCS split across chunks
    edit_csi_t csi; // CSI copy (parser reuses its own)
} edit_token_t;

typedef enum {
    EDIT_VTS_GROUND,
    EDIT_VTS_ESC,
    EDIT_VTS_SS3,
    EDIT_VTS_CSI,
    EDIT_VTS_OSC,
    EDIT_VTS_DCS,
    EDIT_VTS_OSCESC,
    EDIT_VTS_DCSESC,
} edit_vt_state_t;

typedef struct {
    edit_vt_state_t state;
    edit_csi_t csi;
} edit_vt_parser_t;

typedef struct {
    edit_vt_parser_t *parser;
    const uint8_t *input;
    size_t len;
    size_t off;
} edit_vt_stream_t;

void edit_vt_init(edit_vt_parser_t *p);
// Suggested read timeout in ms (50 with trailing ESC, else UINT64_MAX).
uint64_t edit_vt_timeout_ms(const edit_vt_parser_t *p);
void edit_vt_parse(edit_vt_parser_t *p, const uint8_t *input, size_t len, edit_vt_stream_t *s);
// Copies raw bytes at the stream offset; returns bytes copied.
size_t edit_vt_read(edit_vt_stream_t *s, uint8_t *dst, size_t cap);
// Next token; false when exhausted (call again with more input).
bool edit_vt_next(edit_vt_stream_t *s, edit_token_t *out);

#ifdef __cplusplus
}
#endif

#endif
