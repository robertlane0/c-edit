#ifndef EDIT_BASE64_H
#define EDIT_BASE64_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Exact encoded length for src_len bytes; false on overflow or NULL out.
bool edit_base64_encode_len(size_t src_len, size_t *out_len);
// Encodes src into dst (no NUL added); returns 0 ok, -1 bad args, -2 dst too small.
int edit_base64_encode(char *dst, size_t dst_cap, const uint8_t *src, size_t src_len);

#ifdef __cplusplus
}
#endif

#endif
