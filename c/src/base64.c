#include "edit/base64.h"

static const char kCharset[64] = {
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
    'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
    'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
    'w', 'x', 'y', 'z', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/',
};

bool edit_base64_encode_len(size_t src_len, size_t *out_len) {
    if (out_len == NULL) {
        return false;
    }
    size_t groups = src_len / 3 + (src_len % 3 != 0 ? 1 : 0);
    if (groups > SIZE_MAX / 4) {
        *out_len = 0;
        return false;
    }
    *out_len = groups * 4;
    return true;
}

int edit_base64_encode(char *dst, size_t dst_cap, const uint8_t *src, size_t src_len) {
    size_t need = 0;
    if (!edit_base64_encode_len(src_len, &need)) {
        return -1;
    }
    if (need > dst_cap) {
        return -2;
    }
    if (need == 0) {
        return 0;
    }
    if (dst == NULL || src == NULL) {
        return -1;
    }

    size_t si = 0;
    size_t di = 0;
    while (src_len - si > 3) {
        // Big-endian 24-bit group; endian-independent.
        uint32_t val = ((uint32_t)src[si] << 16U) | ((uint32_t)src[si + 1] << 8U) | src[si + 2];
        si += 3;
        dst[di] = kCharset[(val >> 18U) & 0x3FU];
        dst[di + 1] = kCharset[(val >> 12U) & 0x3FU];
        dst[di + 2] = kCharset[(val >> 6U) & 0x3FU];
        dst[di + 3] = kCharset[val & 0x3FU];
        di += 4;
    }

    size_t rem = src_len - si;
    if (rem > 0) {
        uint32_t b0 = src[si];
        uint32_t b1 = rem > 1 ? src[si + 1] : 0;
        uint32_t b2 = rem > 2 ? src[si + 2] : 0;
        uint32_t val = (b0 << 16U) | (b1 << 8U) | b2;
        dst[di] = kCharset[(val >> 18U) & 0x3FU];
        dst[di + 1] = kCharset[(val >> 12U) & 0x3FU];
        dst[di + 2] = rem > 1 ? kCharset[(val >> 6U) & 0x3FU] : '=';
        dst[di + 3] = rem > 2 ? kCharset[val & 0x3FU] : '=';
    }
    return 0;
}
