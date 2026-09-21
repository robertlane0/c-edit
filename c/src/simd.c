#include "edit/simd.h"

#include <string.h>

size_t edit_memchr2(uint8_t n1, uint8_t n2, const uint8_t *hay, size_t len, size_t offset) {
    if (len == 0) {
        return 0;
    }
    if (hay == NULL) {
        return len; // invalid input, defined output
    }
    size_t i = offset < len ? offset : len;
    while (i < len) {
        uint8_t c = hay[i];
        if (c == n1 || c == n2) {
            return i;
        }
        ++i;
    }
    return len;
}

bool edit_memrchr2(uint8_t n1, uint8_t n2, const uint8_t *hay, size_t len, size_t offset,
                   size_t *out) {
    if (out == NULL) {
        return false;
    }
    *out = 0;
    if (len == 0) {
        return false;
    }
    if (hay == NULL) {
        return false;
    }
    size_t end = offset < len ? offset : len;
    while (end > 0) {
        --end;
        uint8_t c = hay[end];
        if (c == n1 || c == n2) {
            *out = end;
            return true;
        }
    }
    return false;
}

// SWAR bulk fill; overlapping tail stores stay in bounds.
static void memset_raw_u64(uint8_t *beg, uint8_t *end, uint64_t v) {
    size_t remaining = (size_t)(end - beg);
    while (remaining >= 32) {
        memcpy(beg, &v, 8);
        memcpy(beg + 8, &v, 8);
        memcpy(beg + 16, &v, 8);
        memcpy(beg + 24, &v, 8);
        beg += 32;
        remaining -= 32;
    }
    while (remaining >= 16) {
        memcpy(beg, &v, 8);
        memcpy(beg + 8, &v, 8);
        beg += 16;
        remaining -= 16;
    }
    if (remaining >= 8) {
        memcpy(beg, &v, 8);
        memcpy(end - 8, &v, 8);
    } else if (remaining >= 4) {
        uint32_t w = (uint32_t)v;
        memcpy(beg, &w, 4);
        memcpy(end - 4, &w, 4);
    } else if (remaining >= 2) {
        uint16_t w = (uint16_t)v;
        memcpy(beg, &w, 2);
        memcpy(end - 2, &w, 2);
    } else if (remaining >= 1) {
        *beg = (uint8_t)v;
    }
}

void edit_memset_u8(uint8_t *dst, uint8_t v, size_t n) {
    if (n == 0 || dst == NULL) {
        return;
    }
    memset(dst, v, n);
}

void edit_memset_u16(uint16_t *dst, uint16_t v, size_t n) {
    if (n == 0 || dst == NULL) {
        return;
    }
    uint64_t w = (uint64_t)v * UINT64_C(0x0001000100010001);
    memset_raw_u64((uint8_t *)dst, (uint8_t *)(dst + n), w);
}

void edit_memset_u32(uint32_t *dst, uint32_t v, size_t n) {
    if (n == 0 || dst == NULL) {
        return;
    }
    uint64_t w = (uint64_t)v * UINT64_C(0x0000000100000001);
    memset_raw_u64((uint8_t *)dst, (uint8_t *)(dst + n), w);
}

void edit_memset_u64(uint64_t *dst, uint64_t v, size_t n) {
    if (n == 0 || dst == NULL) {
        return;
    }
    memset_raw_u64((uint8_t *)dst, (uint8_t *)(dst + n), v);
}
