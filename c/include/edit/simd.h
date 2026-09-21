#ifndef EDIT_SIMD_H
#define EDIT_SIMD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// First index >= offset holding n1/n2, or len if none. Offset clamps to len.
// NULL haystack with len > 0 asserts (debug) and reports not-found.
size_t edit_memchr2(uint8_t n1, uint8_t n2, const uint8_t *hay, size_t len, size_t offset);
// Last hit in [0, offset): true + index in *out; false + *out = 0 if none.
// Offset clamps to len. Serves as next offset for backward iteration.
bool edit_memrchr2(uint8_t n1, uint8_t n2, const uint8_t *hay, size_t len, size_t offset,
                   size_t *out);

// Scalar baseline; auto-vectorizes. SIMD fast paths deferred (needs benches).
// NULL dst with n > 0 asserts (debug) and is a no-op.
void edit_memset_u8(uint8_t *dst, uint8_t v, size_t n);
void edit_memset_u16(uint16_t *dst, uint16_t v, size_t n);
void edit_memset_u32(uint32_t *dst, uint32_t v, size_t n);
void edit_memset_u64(uint64_t *dst, uint64_t v, size_t n);

#ifdef __cplusplus
}
#endif

#endif
