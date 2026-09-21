#ifndef EDIT_HASH_H
#define EDIT_HASH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Wraps on overflow; NULL data with len 0 is empty.
// NULL data with len > 0 asserts (debug) and returns 0.
uint64_t edit_hash(uint64_t seed, const uint8_t *data, size_t len);
// Weak 64x64->64 mix; wraps per unsigned rules.
uint64_t edit_wymix(uint64_t lhs, uint64_t rhs);
// NULL string hashes as empty.
uint64_t edit_hash_str(uint64_t seed, const char *s);

#ifdef __cplusplus
}
#endif

#endif
