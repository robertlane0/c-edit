#ifndef EDIT_ICU_H
#define EDIT_ICU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "edit/apperr.h"

#ifdef __cplusplus
extern "C" {
#endif

// Case folding via libicuuc (dlopen); ASCII-lowercase fallback if missing.
// snprintf-style: returns needed length excl. NUL, (size_t)-1 on bad args.
size_t edit_fold_case(char *dst, size_t cap, const char *src, size_t len);
bool edit_icu_available(void);

// ASCII-only fallback collation, ported 1:1 (cf. Rust compare_strings_ascii).
int edit_compare_ascii(const uint8_t *a, size_t alen, const uint8_t *b, size_t blen);
// Root-collator comparison via libicui18n; ASCII fallback when unavailable.
int edit_compare_strings(const uint8_t *a, size_t alen, const uint8_t *b, size_t blen);
// ICU error name (u_errorName); "" when unavailable. snprintf-style length.
size_t edit_icu_error_text(uint32_t code, char *dst, size_t cap);
// Available encoding names; static storage, at least {"UTF-8"}.
size_t edit_icu_encodings(const char ***out);

// Streaming encoding converter (cf. Rust Converter). Pivot buffer is
// caller-owned; converters close on destroy. Errors via edit_conv_error.
typedef struct {
    void *src_cnv;
    void *dst_cnv;
    uint16_t *pivot;
    size_t pivot_len;
    uint16_t *pivot_src;
    uint16_t *pivot_dst;
    bool reset;
    edit_error_t err;
    bool err_set;
} edit_conv_t;

int edit_conv_init(edit_conv_t *c, const char *src_enc, const char *dst_enc, uint16_t *pivot,
                   size_t pivot_len);
void edit_conv_destroy(edit_conv_t *c);
// One step; flush when inlen == 0. Overflow is success (partial progress).
// 0 ok, -1 error (see edit_conv_error).
int edit_conv_step(edit_conv_t *c, const uint8_t *in, size_t inlen, uint8_t *out, size_t outcap,
                   size_t *in_used, size_t *out_used);
bool edit_conv_error(const edit_conv_t *c, edit_error_t *out_err);

#ifdef __cplusplus
}
#endif

#endif
