#define _POSIX_C_SOURCE 200809L // mkstemp; must precede all headers

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "edit/icu.h"
#include "edit/tbuf.h"

static int checks = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #cond);                                     \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static int write_file_content(const char *path, const uint8_t *data, size_t n) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        return -1;
    }
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(fd, data + off, n - off);
        if (w <= 0) {
            close(fd);
            return -1;
        }
        off += (size_t)w;
    }
    close(fd);
    return 0;
}

static int expect_text(edit_tbuf_t *t, const char *want) {
    size_t n = strlen(want);
    uint8_t buf[512] = {0};
    size_t got = 0;
    size_t off = 0;
    while (off < edit_tbuf_len(t)) {
        const uint8_t *p = NULL;
        size_t k = 0;
        edit_tbuf_read_fwd(t, off, &p, &k);
        if (p == NULL || k == 0 || got + k > sizeof buf) {
            break;
        }
        memcpy(buf + got, p, k);
        got += k;
        off += k;
    }
    ++checks;
    if (got != n || memcmp(buf, want, n) != 0) {
        fprintf(stderr, "FAIL %d: got %zu want %zu\n", __LINE__, got, n);
        return 1;
    }
    return 0;
}

int main(void) {
    // BOM detection vectors (cf. Rust detect_bom).
    CHECK(edit_bom_detect((const uint8_t *)"\xFF\xFE\x00\x00", 4) != NULL &&
          strcmp(edit_bom_detect((const uint8_t *)"\xFF\xFE\x00\x00", 4), "UTF-32LE") == 0);
    CHECK(strcmp(edit_bom_detect((const uint8_t *)"\x00\x00\xFE\xFF", 4), "UTF-32BE") == 0);
    CHECK(strcmp(edit_bom_detect((const uint8_t *)"\x84\x31\x95\x33", 4), "GB18030") == 0);
    CHECK(strcmp(edit_bom_detect((const uint8_t *)"\xEF\xBB\xBFx", 4), "UTF-8") == 0);
    CHECK(strcmp(edit_bom_detect((const uint8_t *)"\xFF\xFEx", 3), "UTF-16LE") == 0);
    CHECK(strcmp(edit_bom_detect((const uint8_t *)"\xFE\xFFx", 3), "UTF-16BE") == 0);
    CHECK(edit_bom_detect((const uint8_t *)"ab", 2) == NULL);
    CHECK(edit_bom_detect((const uint8_t *)"", 0) == NULL);
    CHECK(edit_bom_detect(NULL, 0) == NULL);
    CHECK(edit_bom_detect((const uint8_t *)"\xFF", 1) == NULL); // short read: no guess

    char path[] = "/tmp/opencode-tbuf-test-XXXXXX";
    int tmp = mkstemp(path);
    CHECK(tmp >= 0);
    close(tmp);

    // UTF-8 file with CRLF + tab indentation heuristics.
    static const uint8_t crlf[] = "a\r\n\tb\r\n\tc\r\nd\r\n";
    CHECK(write_file_content(path, crlf, sizeof crlf - 1) == 0);
    {
        edit_tbuf_t t;
        CHECK(edit_tbuf_init(&t, true) == 0);
        int fd = open(path, O_RDONLY);
        CHECK(fd >= 0);
        CHECK(edit_tbuf_read_file(&t, fd, NULL, NULL) == 0);
        close(fd);
        CHECK(expect_text(&t, "a\r\n\tb\r\n\tc\r\nd\r\n") == 0);
        CHECK(edit_tbuf_is_crlf(&t));
        CHECK(edit_tbuf_indent_with_tabs(&t));
        CHECK(edit_tbuf_tab_size(&t) == 4);
        CHECK(strcmp(edit_tbuf_encoding(&t), "UTF-8") == 0);
        CHECK(edit_tbuf_logical_lines(&t) == 5);
        CHECK(!edit_tbuf_is_dirty(&t));
        // Write back and compare bytes.
        int fd2 = open(path, O_WRONLY | O_TRUNC);
        CHECK(fd2 >= 0);
        CHECK(edit_tbuf_write_file(&t, fd2, NULL) == 0);
        close(fd2);
        int fd3 = open(path, O_RDONLY);
        uint8_t back[64] = {0};
        ssize_t n = read(fd3, back, sizeof back);
        close(fd3);
        CHECK(n == (ssize_t)sizeof crlf - 1 && memcmp(back, crlf, (size_t)n) == 0);
        edit_tbuf_destroy(&t);
    }

    // UTF-8 BOM file.
    static const uint8_t bom[] = "\xEF\xBB\xBFhi";
    CHECK(write_file_content(path, bom, sizeof bom - 1) == 0);
    {
        edit_tbuf_t t;
        CHECK(edit_tbuf_init(&t, true) == 0);
        int fd = open(path, O_RDONLY);
        CHECK(edit_tbuf_read_file(&t, fd, NULL, NULL) == 0);
        close(fd);
        CHECK(expect_text(&t, "hi") == 0);
        CHECK(strcmp(edit_tbuf_encoding(&t), "UTF-8 BOM") == 0);
        int fd2 = open(path, O_WRONLY | O_TRUNC);
        CHECK(edit_tbuf_write_file(&t, fd2, NULL) == 0);
        close(fd2);
        int fd3 = open(path, O_RDONLY);
        uint8_t back[16] = {0};
        ssize_t n = read(fd3, back, sizeof back);
        close(fd3);
        CHECK(n == 5 && memcmp(back, bom, 5) == 0);
        edit_tbuf_destroy(&t);
    }

    // Space-indent heuristic picks the most common depth.
    static const uint8_t spaces[] = "a\n  b\n  c\n    d\n";
    CHECK(write_file_content(path, spaces, sizeof spaces - 1) == 0);
    {
        edit_tbuf_t t;
        CHECK(edit_tbuf_init(&t, true) == 0);
        int fd = open(path, O_RDONLY);
        CHECK(edit_tbuf_read_file(&t, fd, NULL, NULL) == 0);
        close(fd);
        CHECK(!edit_tbuf_indent_with_tabs(&t));
        CHECK(edit_tbuf_tab_size(&t) == 2);
        CHECK(!edit_tbuf_is_crlf(&t));
        edit_tbuf_destroy(&t);
    }

    // Empty file.
    CHECK(write_file_content(path, NULL, 0) == 0);
    {
        edit_tbuf_t t;
        CHECK(edit_tbuf_init(&t, true) == 0);
        int fd = open(path, O_RDONLY);
        CHECK(edit_tbuf_read_file(&t, fd, NULL, NULL) == 0);
        close(fd);
        CHECK(edit_tbuf_len(&t) == 0);
        CHECK(edit_tbuf_logical_lines(&t) == 1);
        edit_tbuf_destroy(&t);
    }

    if (edit_icu_available()) {
        // UTF-16LE round-trip through the converter.
        static const uint8_t u16[] = {0xFF, 0xFE, 'h', 0x00, 0xE9, 0x00, 0x0A, 0x00};
        CHECK(write_file_content(path, u16, sizeof u16) == 0);
        edit_tbuf_t t;
        CHECK(edit_tbuf_init(&t, true) == 0);
        int fd = open(path, O_RDONLY);
        CHECK(edit_tbuf_read_file(&t, fd, NULL, NULL) == 0);
        close(fd);
        CHECK(strcmp(edit_tbuf_encoding(&t), "UTF-16LE") == 0);
        CHECK(expect_text(&t, "h\xC3\xA9\n") == 0);
        int fd2 = open(path, O_WRONLY | O_TRUNC);
        CHECK(edit_tbuf_write_file(&t, fd2, NULL) == 0);
        close(fd2);
        int fd3 = open(path, O_RDONLY);
        uint8_t back[32] = {0};
        ssize_t n = read(fd3, back, sizeof back);
        close(fd3);
        // BOM + same content back.
        CHECK(n == (ssize_t)sizeof u16 && memcmp(back, u16, (size_t)n) == 0);
        // Explicit encoding override.
        int fd4 = open(path, O_RDONLY);
        CHECK(edit_tbuf_read_file(&t, fd4, "UTF-16LE", NULL) == 0);
        close(fd4);
        CHECK(expect_text(&t, "h\xC3\xA9\n") == 0);
        edit_tbuf_destroy(&t);
    } else {
        fprintf(stderr, "note: libicu missing, skipping encoding tests\n");
    }

    // Error paths.
    {
        edit_tbuf_t t;
        CHECK(edit_tbuf_init(&t, true) == 0);
        CHECK(edit_tbuf_read_file(NULL, 0, NULL, NULL) != 0);
        CHECK(edit_tbuf_read_file(&t, -1, NULL, NULL) != 0);
        CHECK(edit_tbuf_write_file(NULL, 0, NULL) != 0);
        CHECK(edit_tbuf_write_file(&t, -1, NULL) != 0);
        edit_tbuf_destroy(&t);
    }

    unlink(path);
    printf("test_tfile: %d checks passed\n", checks);
    return 0;
}
