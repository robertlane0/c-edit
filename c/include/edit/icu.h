#ifndef EDIT_ICU_H
#define EDIT_ICU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Case folding via libicuuc (dlopen); ASCII-lowercase fallback if missing.
// snprintf-style: returns needed length excl. NUL, (size_t)-1 on bad args.
size_t edit_fold_case(char *dst, size_t cap, const char *src, size_t len);
bool edit_icu_available(void);

// ASCII-only fallback collation, ported 1:1 (cf. Rust compare_strings_ascii).
int edit_compare_ascii(const uint8_t *a, size_t alen, const uint8_t *b, size_t blen);

#ifdef __cplusplus
}
#endif

#endif
