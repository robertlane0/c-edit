#include <stdio.h>
#include <string.h>

#include "edit/path.h"

static int checks = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #cond);                                     \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static int expect_norm(const char *path, const char *want) {
    char buf[256];
    memset(buf, 0xAA, sizeof buf);
    size_t need = edit_path_normalize(buf, sizeof buf, path);
    ++checks;
    if (need != strlen(want)) {
        fprintf(stderr, "FAIL %d: %s need %zu != %zu\n", __LINE__, path, need, strlen(want));
        return 1;
    }
    ++checks;
    if (strcmp(buf, want) != 0) {
        fprintf(stderr, "FAIL %d: %s -> %s, want %s\n", __LINE__, path, buf, want);
        return 1;
    }
    // Length query agrees.
    ++checks;
    if (edit_path_normalize(NULL, 0, path) != need) {
        fprintf(stderr, "FAIL %d: query mismatch for %s\n", __LINE__, path);
        return 1;
    }
    return 0;
}

int main(void) {
    // Port of Rust path::tests::test_unix.
    CHECK(expect_norm("/a/b/c", "/a/b/c") == 0);
    CHECK(expect_norm("/a/b/c/", "/a/b/c") == 0);
    CHECK(expect_norm("/a/./b", "/a/b") == 0);
    CHECK(expect_norm("/a/b/../c", "/a/c") == 0);
    CHECK(expect_norm("/../../a", "/a") == 0);
    CHECK(expect_norm("/../", "/") == 0);
    CHECK(expect_norm("/a//b/c", "/a/b/c") == 0);
    CHECK(expect_norm("/a/b/c/../../../../d", "/d") == 0);
    CHECK(expect_norm("//", "/") == 0);

    // Extras: root, dots, deep pops, dotfiles, spaces.
    CHECK(expect_norm("/", "/") == 0);
    CHECK(expect_norm("/.", "/") == 0);
    CHECK(expect_norm("/..", "/") == 0);
    CHECK(expect_norm("/a/..", "/") == 0);
    CHECK(expect_norm("/a/../..", "/") == 0);
    CHECK(expect_norm("/a/b/../../c/d", "/c/d") == 0);
    CHECK(expect_norm("/a/././b/./c", "/a/b/c") == 0);
    CHECK(expect_norm("/.../a", "/.../a") == 0);
    CHECK(expect_norm("/.hidden/file", "/.hidden/file") == 0);
    CHECK(expect_norm("/a b/c", "/a b/c") == 0);
    CHECK(expect_norm("///a///b//", "/a/b") == 0);
    CHECK(expect_norm("/a/b/c/d/e/f", "/a/b/c/d/e/f") == 0);
    CHECK(expect_norm("/a/b/c/d/e/f/../../..", "/a/b/c") == 0);

    // Errors: NULL, empty, relative.
    CHECK(edit_path_normalize(NULL, 0, NULL) == (size_t)-1);
    char tmp[8];
    CHECK(edit_path_normalize(tmp, sizeof tmp, NULL) == (size_t)-1);
    CHECK(edit_path_normalize(tmp, sizeof tmp, "") == (size_t)-1);
    CHECK(edit_path_normalize(tmp, sizeof tmp, "a/b") == (size_t)-1);
    CHECK(edit_path_normalize(tmp, sizeof tmp, "./a") == (size_t)-1);

    // Truncation stays NUL-terminated and reports full length.
    char small[4];
    size_t need = edit_path_normalize(small, sizeof small, "/a/b/c");
    CHECK(need == 6);
    CHECK(small[sizeof small - 1] == '\0');
    CHECK(strncmp(small, "/a/", 3) == 0);
    // cap == 1 writes only NUL.
    char one[1];
    CHECK(edit_path_normalize(one, sizeof one, "/ab") == 3);
    CHECK(one[0] == '\0');

    printf("test_path: %d checks passed\n", checks);
    return 0;
}
