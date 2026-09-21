#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "edit/base64.h"

static int checks = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #cond);                                     \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

// Encodes into a stack buffer and compares with expected C string.
static int expect_enc(const uint8_t *src, size_t n, const char *want) {
    size_t need = 0;
    if (!edit_base64_encode_len(n, &need)) {
        fprintf(stderr, "FAIL %d: len overflow\n", __LINE__);
        return 1;
    }
    ++checks;
    if (need != strlen(want)) {
        fprintf(stderr, "FAIL %d: need %zu != %zu\n", __LINE__, need, strlen(want));
        return 1;
    }
    char buf[64];
    if (need > sizeof buf) {
        fprintf(stderr, "FAIL %d: vector too long\n", __LINE__);
        return 1;
    }
    ++checks;
    if (edit_base64_encode(buf, sizeof buf, src, n) != 0) {
        fprintf(stderr, "FAIL %d: encode failed\n", __LINE__);
        return 1;
    }
    ++checks;
    if (memcmp(buf, want, need) != 0) {
        fprintf(stderr, "FAIL %d: mismatch for len %zu\n", __LINE__, n);
        return 1;
    }
    return 0;
}

int main(void) {
    // Port of Rust base64::tests::test_basic.
    static const uint8_t alpha[] = "abcdefghijklmNOPQRSTUVWXYZ";
    static const char *want[] = {
        "",
        "YQ==",
        "YWI=",
        "YWJj",
        "YWJjZA==",
        "YWJjZGU=",
        "YWJjZGVm",
        "YWJjZGVmZw==",
        "YWJjZGVmZ2g=",
        "YWJjZGVmZ2hp",
        "YWJjZGVmZ2hpag==",
        "YWJjZGVmZ2hpams=",
        "YWJjZGVmZ2hpamts",
        "YWJjZGVmZ2hpamtsbQ==",
        "YWJjZGVmZ2hpamtsbU4=",
        "YWJjZGVmZ2hpamtsbU5P",
        "YWJjZGVmZ2hpamtsbU5PUA==",
        "YWJjZGVmZ2hpamtsbU5PUFE=",
        "YWJjZGVmZ2hpamtsbU5PUFFS",
        "YWJjZGVmZ2hpamtsbU5PUFFSUw==",
        "YWJjZGVmZ2hpamtsbU5PUFFSU1Q=",
        "YWJjZGVmZ2hpamtsbU5PUFFSU1RV",
        "YWJjZGVmZ2hpamtsbU5PUFFSU1RVVg==",
        "YWJjZGVmZ2hpamtsbU5PUFFSU1RVVlc=",
        "YWJjZGVmZ2hpamtsbU5PUFFSU1RVVldY",
        "YWJjZGVmZ2hpamtsbU5PUFFSU1RVVldYWQ==",
        "YWJjZGVmZ2hpamtsbU5PUFFSU1RVVldYWVo=",
    };
    for (size_t n = 0; n < sizeof want / sizeof want[0]; ++n) {
        if (expect_enc(alpha, n, want[n]) != 0) {
            return 1;
        }
    }

    // Length table: div_ceil(n,3)*4.
    for (size_t n = 0; n < 64; ++n) {
        size_t need = 0;
        CHECK(edit_base64_encode_len(n, &need));
        CHECK(need == (n + 2) / 3 * 4);
    }

    // Binary incl. NUL and 0xFF round-trips against known values.
    static const uint8_t bin[] = {0x00, 0xFF, 0x10, 0x83, 0x00};
    CHECK(expect_enc(bin, 1, "AA==") == 0);
    CHECK(expect_enc(bin, 2, "AP8=") == 0);
    CHECK(expect_enc(bin, 3, "AP8Q") == 0);
    CHECK(expect_enc(bin, 4, "AP8Qgw==") == 0);
    CHECK(expect_enc(bin, 5, "AP8QgwA=") == 0);

    // Error paths.
    char tiny[3];
    size_t need = 0;
    CHECK(edit_base64_encode_len(3, &need) && need == 4);
    CHECK(edit_base64_encode(tiny, sizeof tiny, (const uint8_t *)"abc", 3) == -2);
    CHECK(edit_base64_encode(NULL, 0, NULL, 0) == 0);
    CHECK(edit_base64_encode_len(0, NULL) == false);
    CHECK(edit_base64_encode(NULL, 4, (const uint8_t *)"abc", 3) == -1);

    printf("test_base64: %d checks passed\n", checks);
    return 0;
}
