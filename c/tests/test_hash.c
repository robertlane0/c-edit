#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "edit/hash.h"

static int checks = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                        \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

int main(void) {
    // Vectors from Rust edit::hash (seed, bytes, expected).
    static const uint8_t d_ff64[64] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    };
    uint8_t seq100[100];
    for (size_t i = 0; i < 100; ++i) {
        seq100[i] = (uint8_t)i;
    }
    static const uint8_t z16[16] = {0};
    static const uint8_t o17[17] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    static const uint8_t s48[48] = {
        7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    };
    static const uint8_t s49[49] = {
        7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    };

    CHECK(edit_hash(0, NULL, 0) == UINT64_C(0x42bc986dc5eec4d3));
    CHECK(edit_hash(0, (const uint8_t *)"a", 1) == UINT64_C(0x6cf84e5a2465e867));
    CHECK(edit_hash(0, (const uint8_t *)"ab", 2) == UINT64_C(0x172ba773b8ebb6d8));
    CHECK(edit_hash(0, (const uint8_t *)"abc", 3) == UINT64_C(0xb4808df22d44ffcf));
    CHECK(edit_hash(0, (const uint8_t *)"abcd", 4) == UINT64_C(0xe73573b4c2ddfea0));
    CHECK(edit_hash(0, (const uint8_t *)"abcde", 5) == UINT64_C(0x4908f54b787f5ced));
    CHECK(edit_hash(12345, (const uint8_t *)"hello world", 11) == UINT64_C(0x1247503cf037dbb0));
    CHECK(edit_hash(0, z16, 16) == UINT64_C(0x43d8b349ad67b191));
    CHECK(edit_hash(0, o17, 17) == UINT64_C(0x4e903d6c2a14ff8b));
    CHECK(edit_hash(42, s48, 48) == UINT64_C(0xceb1c0b290bded2e));
    CHECK(edit_hash(42, s49, 49) == UINT64_C(0x8f2f8fe59cbb91eb));
    CHECK(edit_hash(1, d_ff64, 64) == UINT64_C(0x224d2e823273dd9f));
    CHECK(edit_hash(UINT64_C(0x0123456789ABCDEF), seq100, 100) == UINT64_C(0x90beb07bfa967129));
    CHECK(edit_hash(0, (const uint8_t *)"The quick brown fox jumps over the lazy dog", 43) ==
          UINT64_C(0xd986947fb5be3867));

    CHECK(edit_wymix(0, 0) == 0);
    CHECK(edit_wymix(1, 2) == 2);
    CHECK(edit_wymix(UINT64_MAX, UINT64_MAX) == UINT64_MAX);

    CHECK(edit_hash_str(0, "") == edit_hash(0, NULL, 0));
    CHECK(edit_hash_str(0, "abc") == edit_hash(0, (const uint8_t *)"abc", 3));
    CHECK(edit_hash_str(0, NULL) == edit_hash(0, NULL, 0));
    CHECK(edit_hash(0, NULL, 5) == 0); // invalid input, defined output
    // Byte-wise determinism across boundary lengths.
    uint8_t buf[96];
    for (size_t i = 0; i < sizeof buf; ++i) {
        buf[i] = (uint8_t)(i * 31U + 7U);
    }
    for (size_t n = 0; n <= sizeof buf; ++n) {
        uint64_t h1 = edit_hash(99, buf, n);
        uint64_t h2 = edit_hash(99, buf, n);
        CHECK(h1 == h2);
        (void)h1;
        (void)h2;
    }

    printf("test_hash: %d checks passed\n", checks);
    return 0;
}
