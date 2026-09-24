#include "edit/simd.h"

#include <stdint.h>
#include <string.h>

#if defined(__x86_64__) || defined(__i386__)
#define EDIT_SIMD_X86 1
#include <immintrin.h>
#endif

// AVX2 paths mirror Rust's runtime-dispatched memchr2/memrchr2 (x86 only).
// Reads never cross the end of the buffer: 32-byte chunks are only loaded
// while at least 32 bytes remain, and the tail is scanned byte-wise.
#ifdef EDIT_SIMD_X86
__attribute__((target("avx2"))) static size_t
memchr2_avx2(uint8_t n1, uint8_t n2, const uint8_t *hay, size_t len, size_t offset) {
    __m256i v1 = _mm256_set1_epi8((char)n1);
    __m256i v2 = _mm256_set1_epi8((char)n2);
    size_t i = offset;
    while (len - i >= 32) {
        __m256i v = _mm256_loadu_si256((const __m256i *)(const void *)(hay + i));
        __m256i eq = _mm256_or_si256(_mm256_cmpeq_epi8(v, v1), _mm256_cmpeq_epi8(v, v2));
        uint32_t mask = (uint32_t)_mm256_movemask_epi8(eq);
        if (mask != 0) {
            return i + (size_t)__builtin_ctz(mask);
        }
        i += 32;
    }
    while (i < len) {
        if (hay[i] == n1 || hay[i] == n2) {
            return i;
        }
        ++i;
    }
    return len;
}

__attribute__((target("avx2"))) static bool
memrchr2_avx2(uint8_t n1, uint8_t n2, const uint8_t *hay, size_t len, size_t offset, size_t *out) {
    (void)len;
    __m256i v1 = _mm256_set1_epi8((char)n1);
    __m256i v2 = _mm256_set1_epi8((char)n2);
    size_t i = offset;
    while (i >= 32) {
        __m256i v = _mm256_loadu_si256((const __m256i *)(const void *)(hay + i - 32));
        __m256i eq = _mm256_or_si256(_mm256_cmpeq_epi8(v, v1), _mm256_cmpeq_epi8(v, v2));
        uint32_t mask = (uint32_t)_mm256_movemask_epi8(eq);
        if (mask != 0) {
            *out = i - 32 + (31 - (size_t)__builtin_clz(mask));
            return true;
        }
        i -= 32;
    }
    while (i > 0) {
        --i;
        if (hay[i] == n1 || hay[i] == n2) {
            *out = i;
            return true;
        }
    }
    return false;
}
#endif

size_t edit_memchr2(uint8_t n1, uint8_t n2, const uint8_t *hay, size_t len, size_t offset) {
    if (len == 0) {
        return 0;
    }
    if (hay == NULL) {
        return len; // invalid input, defined output
    }
    size_t i = offset < len ? offset : len;
#ifdef EDIT_SIMD_X86
    // Only pay dispatch + vector setup when a full 32-byte chunk is in range.
    if (len - i >= 32 && __builtin_cpu_supports("avx2")) {
        return memchr2_avx2(n1, n2, hay, len, i);
    }
#endif
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
#ifdef EDIT_SIMD_X86
    if (end >= 32 && __builtin_cpu_supports("avx2")) {
        return memrchr2_avx2(n1, n2, hay, len, end, out);
    }
#endif
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
