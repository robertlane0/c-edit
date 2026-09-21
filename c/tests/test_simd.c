#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "edit/simd.h"

#ifdef __linux__
#include <sys/mman.h>
#include <unistd.h>
#endif

static int checks = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #cond);                                     \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static int check_memchr2(uint8_t n1, uint8_t n2, const uint8_t *hay, size_t len, size_t off,
                         size_t want) {
    size_t got = edit_memchr2(n1, n2, hay, len, off);
    ++checks;
    if (got != want) {
        fprintf(stderr, "FAIL %d: memchr2 got %zu want %zu\n", __LINE__, got, want);
        return 1;
    }
    return 0;
}

static int check_memrchr2(uint8_t n1, uint8_t n2, const uint8_t *hay, size_t len, size_t off,
                          int want_hit, size_t want_idx) {
    size_t idx = 999999;
    bool hit = edit_memrchr2(n1, n2, hay, len, off, &idx);
    ++checks;
    if (hit != (want_hit != 0) || (hit && idx != want_idx) || (!hit && idx != 0)) {
        fprintf(stderr, "FAIL %d: memrchr2 hit %d idx %zu\n", __LINE__, (int)hit, idx);
        return 1;
    }
    return 0;
}

int main(void) {
    // Port of Rust memchr2 tests.
    CHECK(check_memchr2('a', 'b', (const uint8_t *)"", 0, 0, 0) == 0);
    static const uint8_t alpha43[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQ";
    CHECK(check_memchr2('a', 'z', alpha43, 43, 0, 0) == 0);
    CHECK(check_memchr2('p', 'q', alpha43, 43, 0, 15) == 0);
    CHECK(check_memchr2('Q', 'Z', alpha43, 43, 0, 42) == 0);
    CHECK(check_memchr2('0', '9', alpha43, 43, 0, 43) == 0);

    static const uint8_t rep40[] = "abcdefghabcdefghabcdefghabcdefghabcdefgh";
    CHECK(check_memchr2('a', 'b', rep40, 40, 0, 0) == 0);
    CHECK(check_memchr2('a', 'b', rep40, 40, 1, 1) == 0);
    CHECK(check_memchr2('a', 'b', rep40, 40, 2, 8) == 0);
    CHECK(check_memchr2('a', 'b', rep40, 40, 9, 9) == 0);
    CHECK(check_memchr2('a', 'b', rep40, 40, 16, 16) == 0);
    CHECK(check_memchr2('a', 'b', rep40, 40, 41, 40) == 0); // offset clamps

    // Port of Rust memrchr2 tests.
    CHECK(check_memrchr2('a', 'b', (const uint8_t *)"", 0, 0, 0, 0) == 0);
    CHECK(check_memrchr2('Q', 'P', alpha43, 43, 43, 1, 42) == 0);
    CHECK(check_memrchr2('p', 'o', alpha43, 43, 43, 1, 15) == 0);
    CHECK(check_memrchr2('a', 'b', alpha43, 43, 43, 1, 1) == 0);
    CHECK(check_memrchr2('0', '9', alpha43, 43, 43, 0, 0) == 0);
    CHECK(check_memrchr2('h', 'g', rep40, 40, 40, 1, 39) == 0);
    CHECK(check_memrchr2('h', 'g', rep40, 40, 39, 1, 38) == 0);
    CHECK(check_memrchr2('a', 'b', rep40, 40, 9, 1, 8) == 0);
    CHECK(check_memrchr2('a', 'b', rep40, 40, 1, 1, 0) == 0);
    CHECK(check_memrchr2('a', 'b', rep40, 40, 0, 0, 0) == 0);

    // Same needle twice behaves like single-needle search.
    CHECK(check_memchr2('x', 'x', alpha43, 43, 0, 23) == 0);
    CHECK(check_memrchr2('x', 'x', alpha43, 43, 43, 1, 23) == 0);

    // Port of Rust memset tests.
    uint8_t b8[1024];
    memset(b8, 0x55, sizeof b8);
    edit_memset_u8(b8, 0xAA, 0);
    CHECK(b8[0] == 0x55); // empty fill is a no-op
    for (size_t len = 0; len < 40; ++len) {
        memset(b8, (int)(len & 0x7F), sizeof b8);
        edit_memset_u8(b8, 0xAA, len);
        for (size_t i = 0; i < len; ++i) {
            CHECK(b8[i] == 0xAA);
        }
        CHECK(b8[len > 0 ? len : 0] == (uint8_t)(len & 0x7F) || len == sizeof b8);
    }
    edit_memset_u8(b8, 0xFF, 1024);
    for (size_t i = 0; i < sizeof b8; ++i) {
        CHECK(b8[i] == 0xFF);
    }

    uint16_t b16[512];
    uint32_t b32[256];
    uint64_t b64[128];
    static const size_t lens[] = {0, 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 31, 32, 33, 100, 128};
    for (size_t li = 0; li < sizeof lens / sizeof lens[0]; ++li) {
        size_t n = lens[li] > 128 ? 128 : lens[li];
        for (size_t i = 0; i < 128; ++i) {
            b16[i] = (uint16_t)(0xBEEF ^ i);
            b32[i] = 0xCAFEBABEu ^ (uint32_t)i;
            b64[i] = UINT64_C(0x1234567890ABCDEF) ^ (uint64_t)i;
        }
        edit_memset_u16(b16, 0xBEEF, n > 512 ? 512 : n);
        edit_memset_u32(b32, 0xCAFEBABE, n > 256 ? 256 : n);
        edit_memset_u64(b64, UINT64_C(0x1234567890ABCDEF), n);
        size_t n16 = n > 512 ? 512 : n;
        size_t n32 = n > 256 ? 256 : n;
        for (size_t i = 0; i < n16; ++i) {
            CHECK(b16[i] == 0xBEEF);
        }
        for (size_t i = 0; i < n32; ++i) {
            CHECK(b32[i] == 0xCAFEBABE);
        }
        for (size_t i = 0; i < n; ++i) {
            CHECK(b64[i] == UINT64_C(0x1234567890ABCDEF));
        }
    }
    // Signed bit patterns (two's complement).
    int16_t s16[8];
    int32_t s32[8];
    edit_memset_u16((uint16_t *)s16, (uint16_t)(int16_t)-2, 8);
    edit_memset_u32((uint32_t *)s32, (uint32_t)(int32_t)-3, 8);
    for (int i = 0; i < 8; ++i) {
        CHECK(s16[i] == -2);
        CHECK(s32[i] == -3);
    }
    // Unaligned start within a buffer.
    uint8_t ubuf[15];
    memset(ubuf, 0, sizeof ubuf);
    edit_memset_u8(ubuf + 3, 0x5A, 7);
    for (int i = 0; i < 15; ++i) {
        CHECK(ubuf[i] == (i >= 3 && i < 10 ? 0x5A : 0));
    }

#ifdef __linux__
    // Guard pages: no reads past either end (fails with SIGSEGV otherwise).
    long ps = sysconf(_SC_PAGESIZE);
    CHECK(ps > 0);
    size_t page = (size_t)ps;
    uint8_t *base =
        (uint8_t *)mmap(NULL, page * 3, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(base != MAP_FAILED);
    CHECK(mprotect(base, page, PROT_NONE) == 0);
    CHECK(mprotect(base + page * 2, page, PROT_NONE) == 0);
    uint8_t *pg = base + page;
    memset(pg, 'a', page);
    CHECK(edit_memchr2('\0', '\0', pg + page - 40, 40, 0) == 40);
    CHECK(edit_memchr2('\0', '\0', pg, 10, 0) == 10);
    size_t idx = 0;
    CHECK(!edit_memrchr2('\0', '\0', pg + page - 10, 10, 10, &idx));
    CHECK(!edit_memrchr2('\0', '\0', pg, 40, 40, &idx));
    CHECK(munmap(base, page * 3) == 0);
#endif

    printf("test_simd: %d checks passed\n", checks);
    return 0;
}
