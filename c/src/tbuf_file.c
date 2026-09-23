#include "edit/tbuf.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "edit/icu.h"
#include "tbuf_priv.h"

#define TBUF_BOM_MAX 4
#define TBUF_IO_CHUNK ((size_t)128 * 1024)
#define TBUF_CVT_CHUNK ((size_t)8 * 1024)
#define TBUF_PIVOT ((size_t)4 * 1024)

const char *edit_bom_detect(const uint8_t *bytes, size_t len) {
    if (bytes == NULL) {
        return NULL;
    }
    if (len >= 4) {
        if (memcmp(bytes, "\xFF\xFE\x00\x00", 4) == 0) {
            return "UTF-32LE";
        }
        if (memcmp(bytes, "\x00\x00\xFE\xFF", 4) == 0) {
            return "UTF-32BE";
        }
        if (memcmp(bytes, "\x84\x31\x95\x33", 4) == 0) {
            return "GB18030";
        }
    }
    if (len >= 3 && memcmp(bytes, "\xEF\xBB\xBF", 3) == 0) {
        return "UTF-8";
    }
    if (len >= 2) {
        if (memcmp(bytes, "\xFF\xFE", 2) == 0) {
            return "UTF-16LE";
        }
        if (memcmp(bytes, "\xFE\xFF", 2) == 0) {
            return "UTF-16BE";
        }
    }
    return NULL;
}

// read() with EINTR retry; 0 = EOF, -1 = error (errno kept).
static ssize_t read_full(int fd, uint8_t *buf, size_t cap) {
    if (cap == 0) {
        return 0;
    }
    for (;;) {
        ssize_t n = read(fd, buf, cap);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        return n;
    }
}

// write() loop with EINTR retry; false on error/partial.
static bool write_full(int fd, const uint8_t *buf, size_t len) {
    while (len > 0) {
        ssize_t n = write(fd, buf, len);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (n == 0) {
            return false;
        }
        buf += (size_t)n;
        len -= (size_t)n;
    }
    return true;
}

static void set_encoding(edit_tbuf_t *t, const char *enc) {
    size_t n = strlen(enc);
    if (n > sizeof t->encoding - 1) {
        n = sizeof t->encoding - 1;
    }
    memcpy(t->encoding, enc, n);
    t->encoding[n] = '\0';
}

// Post-load heuristics + recalc (mirrors read_file tail).
static void analyze_content(edit_tbuf_t *t) {
    const uint8_t *chunk = NULL;
    size_t chunk_len = 0;
    edit_gap_read_fwd(&t->buffer, 0, &chunk, &chunk_len);
    if (chunk == NULL) {
        chunk_len = 0;
    }
    size_t offset = 0;
    int32_t lines = 0;
    int32_t crlf_count = 0;
    int32_t tab_indents = 0;
    int32_t space_indents = 0;
    int32_t hist[7] = {0, 0, 0, 0, 0, 0, 0};
    for (;;) {
        if (offset < chunk_len && chunk[offset] == '\t') {
            tab_indents += 1;
        } else {
            size_t spaces = 0;
            while (spaces < 9 && offset + spaces < chunk_len && chunk[offset + spaces] == ' ') {
                spaces += 1;
            }
            if (spaces >= 2 && spaces <= 8) {
                space_indents += 1;
                hist[spaces - 2] += 1;
                if ((spaces & 4) != 0) {
                    hist[0] += 1;
                }
                if (spaces == 6 || spaces == 8) {
                    hist[spaces / 2 - 2] += 1;
                }
            }
        }
        size_t delta = 0;
        int32_t line = lines;
        edit_newlines_forward(chunk, chunk_len, offset, lines, lines + 1, &delta, &line);
        offset = delta;
        lines = line;
        if (offset >= 2 && offset <= chunk_len && chunk[offset - 2] == '\r' &&
            chunk[offset - 1] == '\n') {
            crlf_count += 1;
        }
        if (offset >= chunk_len || lines >= 1000) {
            break;
        }
    }
    t->newlines_crlf = crlf_count >= lines / 2;
    if (tab_indents > space_indents) {
        t->indent_tabs = true;
        t->tab_size = 4;
    } else {
        t->indent_tabs = false;
        int32_t max = 1;
        int32_t tab_size = 4;
        for (size_t i = 0; i < 7; ++i) {
            if (hist[i] >= max) {
                max = hist[i];
                tab_size = (int32_t)i + 2;
            }
        }
        t->tab_size = tab_size;
    }
    if (offset < chunk_len) {
        int32_t rest = lines;
        edit_newlines_forward(chunk, chunk_len, offset, lines, INT32_MAX, &offset, &rest);
        lines = rest;
    }
    t->logical_lines = lines + 1;
    t->visual_lines = t->logical_lines;
    // Recalc: drop history/selection, clean, reflow.
    for (size_t i = 0; i < 2; ++i) {
        edit_hist_t *stack = i == 0 ? t->undo : t->redo;
        size_t len = i == 0 ? t->undo_len : t->redo_len;
        for (size_t k = 0; k < len; ++k) {
            free(stack[k].deleted);
            free(stack[k].added);
        }
        if (i == 0) {
            t->undo_len = 0;
        } else {
            t->redo_len = 0;
        }
    }
    t->hist_last = 0;
    memset(&t->cursor, 0, sizeof t->cursor);
    t->has_render_cursor = false;
    edit_point_t z = {0, 0};
    tbuf_set_selection(t, false, z, z);
    t->save_generation = edit_gap_generation(&t->buffer);
    tbuf_reflow(t, true);
}

int edit_tbuf_read_file(edit_tbuf_t *t, int fd, const char *encoding, edit_error_t *err) {
    if (t == NULL || fd < 0) {
        return -1;
    }
    uint8_t head[TBUF_BOM_MAX];
    size_t head_len = 0;
    bool done = false;
    while (head_len < TBUF_BOM_MAX) {
        ssize_t n = read_full(fd, head + head_len, TBUF_BOM_MAX - head_len);
        if (n < 0) {
            if (err != NULL) {
                *err = edit_error_sys((uint32_t)errno);
            }
            return -1;
        }
        if (n == 0) {
            done = true;
            break;
        }
        head_len += (size_t)n;
    }

    const char *enc = encoding;
    if (enc == NULL) {
        const char *bom = edit_bom_detect(head, head_len);
        enc = bom != NULL ? bom : "UTF-8";
    }
    set_encoding(t, enc);
    edit_gap_clear(&t->buffer);

    if (strcmp(t->encoding, "UTF-8") == 0) {
        size_t skip = 0;
        if (head_len >= 3 && memcmp(head, "\xEF\xBB\xBF", 3) == 0) {
            skip = 3;
            set_encoding(t, "UTF-8 BOM");
        }
        if (head_len > skip) {
            size_t gap = 0;
            uint8_t *dst = edit_gap_allocate(&t->buffer, 0, head_len - skip, 0, &gap);
            size_t k = head_len - skip < gap ? head_len - skip : gap;
            if (dst != NULL && k > 0) {
                memcpy(dst, head + skip, k);
            }
            edit_gap_commit(&t->buffer, k);
            if (k != head_len - skip) {
                done = true; // OOM: keep the prefix (mirrors Rust empty-gap break)
            }
        }
        if (!done) {
            uint8_t *buf = (uint8_t *)malloc(TBUF_IO_CHUNK);
            if (buf == NULL) {
                if (err != NULL) {
                    *err = edit_error_sys((uint32_t)ENOMEM);
                }
                return -1;
            }
            int rc = 0;
            for (;;) {
                ssize_t n = read_full(fd, buf, TBUF_IO_CHUNK);
                if (n < 0) {
                    if (err != NULL) {
                        *err = edit_error_sys((uint32_t)errno);
                    }
                    rc = -1;
                    break;
                }
                if (n == 0) {
                    break;
                }
                size_t gap = 0;
                uint8_t *dst =
                    edit_gap_allocate(&t->buffer, edit_gap_len(&t->buffer), (size_t)n, 0, &gap);
                size_t k = (size_t)n < gap ? (size_t)n : gap;
                if (dst != NULL && k > 0) {
                    memcpy(dst, buf, k);
                }
                edit_gap_commit(&t->buffer, k);
                if (k != (size_t)n) {
                    break; // OOM: keep what fit
                }
            }
            free(buf);
            if (rc != 0) {
                return rc;
            }
        }
    } else {
        if (!edit_icu_available()) {
            if (err != NULL) {
                *err = edit_error_app(0);
            }
            return -1;
        }
        uint16_t *pivot = (uint16_t *)malloc(TBUF_PIVOT * sizeof *pivot);
        if (pivot == NULL) {
            if (err != NULL) {
                *err = edit_error_sys((uint32_t)ENOMEM);
            }
            return -1;
        }
        edit_conv_t conv;
        memset(&conv, 0, sizeof conv);
        int rc = 0;
        if (edit_conv_init(&conv, t->encoding, "UTF-8", pivot, TBUF_PIVOT) != 0) {
            edit_conv_error(&conv, err);
            free(pivot);
            return -1;
        }
        // Convert the head chunk first (BOM stripped from output once).
        size_t hlen = head_len;
        bool first = true;
        uint8_t out[TBUF_CVT_CHUNK];
        while (hlen > 0) {
            size_t iu = 0;
            size_t ou = 0;
            if (edit_conv_step(&conv, head, hlen, out, sizeof out, &iu, &ou) != 0) {
                edit_conv_error(&conv, err);
                rc = -1;
                break;
            }
            size_t at = 0;
            if (first) {
                first = false;
                if (ou >= 3 && memcmp(out, "\xEF\xBB\xBF", 3) == 0) {
                    at = 3;
                }
            }
            if (ou > at) {
                size_t gap = 0;
                uint8_t *dst =
                    edit_gap_allocate(&t->buffer, edit_gap_len(&t->buffer), ou - at, 0, &gap);
                size_t k = ou - at < gap ? ou - at : gap;
                if (dst != NULL && k > 0) {
                    memcpy(dst, out + at, k);
                }
                edit_gap_commit(&t->buffer, k);
            }
            if (iu == 0) {
                break; // needs more input; fall through to file loop
            }
            memmove(head, head + iu, hlen - iu);
            hlen -= iu;
        }
        uint8_t inbuf[TBUF_IO_CHUNK];
        size_t inlen = 0;
        while (rc == 0) {
            if (!done) {
                if (inlen < sizeof inbuf) {
                    ssize_t n = read_full(fd, inbuf + inlen, sizeof inbuf - inlen);
                    if (n < 0) {
                        if (err != NULL) {
                            *err = edit_error_sys((uint32_t)errno);
                        }
                        rc = -1;
                        break;
                    }
                    if (n == 0) {
                        done = true;
                    } else {
                        inlen += (size_t)n;
                    }
                }
            }
            size_t gap = 0;
            // Convert into a stack buffer, then append (gap may be smaller).
            size_t iu = 0;
            size_t ou = 0;
            if (inlen > 0) {
                if (edit_conv_step(&conv, inbuf, inlen, out, sizeof out, &iu, &ou) != 0) {
                    edit_conv_error(&conv, err);
                    rc = -1;
                    break;
                }
                memmove(inbuf, inbuf + iu, inlen - iu);
                inlen -= iu;
                if (iu == 0 && ou == 0 && !done) {
                    continue; // transient pivot stall; read more input
                }
                if (iu == 0 && ou == 0) {
                    break; // stuck with no input left; keep partial content
                }
            } else if (!done) {
                continue;
            }
            if (ou > 0) {
                uint8_t *dst = edit_gap_allocate(&t->buffer, edit_gap_len(&t->buffer), ou, 0, &gap);
                size_t k = ou < gap ? ou : gap;
                if (dst != NULL && k > 0) {
                    memcpy(dst, out, k);
                }
                edit_gap_commit(&t->buffer, k);
            }
            if (done && inlen == 0) {
                // Final flush for stateful encodings.
                if (edit_conv_step(&conv, NULL, 0, out, sizeof out, &iu, &ou) != 0) {
                    edit_conv_error(&conv, err);
                    rc = -1;
                    break;
                }
                if (ou > 0) {
                    uint8_t *dst =
                        edit_gap_allocate(&t->buffer, edit_gap_len(&t->buffer), ou, 0, &gap);
                    size_t k = ou < gap ? ou : gap;
                    if (dst != NULL && k > 0) {
                        memcpy(dst, out, k);
                    }
                    edit_gap_commit(&t->buffer, k);
                }
                break;
            }
            (void)gap;
        }
        edit_conv_destroy(&conv);
        free(pivot);
        if (rc != 0) {
            return rc;
        }
    }

    analyze_content(t);
    return 0;
}

int edit_tbuf_write_file(edit_tbuf_t *t, int fd, edit_error_t *err) {
    if (t == NULL || fd < 0) {
        return -1;
    }
    if (strncmp(t->encoding, "UTF-8", 5) == 0) {
        if (strcmp(t->encoding, "UTF-8 BOM") == 0) {
            static const uint8_t bom[3] = {0xEF, 0xBB, 0xBF};
            if (!write_full(fd, bom, 3)) {
                if (err != NULL) {
                    *err = edit_error_sys((uint32_t)errno);
                }
                return -1;
            }
        }
        size_t off = 0;
        size_t len = edit_gap_len(&t->buffer);
        while (off < len) {
            const uint8_t *chunk = NULL;
            size_t chunk_len = 0;
            edit_gap_read_fwd(&t->buffer, off, &chunk, &chunk_len);
            if (chunk == NULL || chunk_len == 0) {
                break;
            }
            if (!write_full(fd, chunk, chunk_len)) {
                if (err != NULL) {
                    *err = edit_error_sys((uint32_t)errno);
                }
                return -1;
            }
            off += chunk_len;
        }
    } else {
        if (!edit_icu_available()) {
            if (err != NULL) {
                *err = edit_error_app(0);
            }
            return -1;
        }
        uint16_t *pivot = (uint16_t *)malloc(TBUF_PIVOT * sizeof *pivot);
        uint8_t *buf = (uint8_t *)malloc(TBUF_CVT_CHUNK);
        if (pivot == NULL || buf == NULL) {
            free(pivot);
            free(buf);
            if (err != NULL) {
                *err = edit_error_sys((uint32_t)ENOMEM);
            }
            return -1;
        }
        edit_conv_t conv;
        memset(&conv, 0, sizeof conv);
        int rc = 0;
        if (edit_conv_init(&conv, "UTF-8", t->encoding, pivot, TBUF_PIVOT) != 0) {
            edit_conv_error(&conv, err);
            rc = -1;
        }
        if (rc == 0 &&
            (strncmp(t->encoding, "UTF-16", 6) == 0 || strncmp(t->encoding, "UTF-32", 6) == 0 ||
             strcmp(t->encoding, "GB18030") == 0)) {
            size_t iu = 0;
            size_t ou = 0;
            static const uint8_t bom[] = {0xEF, 0xBB, 0xBF};
            if (edit_conv_step(&conv, bom, 3, buf, TBUF_CVT_CHUNK, &iu, &ou) != 0) {
                edit_conv_error(&conv, err);
                rc = -1;
            } else if (ou > 0 && !write_full(fd, buf, ou)) {
                if (err != NULL) {
                    *err = edit_error_sys((uint32_t)errno);
                }
                rc = -1;
            }
        }
        size_t off = 0;
        size_t len = edit_gap_len(&t->buffer);
        while (rc == 0 && off < len) {
            const uint8_t *chunk = NULL;
            size_t chunk_len = 0;
            edit_gap_read_fwd(&t->buffer, off, &chunk, &chunk_len);
            if (chunk == NULL || chunk_len == 0) {
                break;
            }
            // Convert the chunk piecemeal (output buffer may be smaller).
            size_t used = 0;
            while (used < chunk_len) {
                size_t iu = 0;
                size_t ou = 0;
                if (edit_conv_step(&conv, chunk + used, chunk_len - used, buf, TBUF_CVT_CHUNK, &iu,
                                   &ou) != 0) {
                    edit_conv_error(&conv, err);
                    rc = -1;
                    break;
                }
                if (ou > 0 && !write_full(fd, buf, ou)) {
                    if (err != NULL) {
                        *err = edit_error_sys((uint32_t)errno);
                    }
                    rc = -1;
                    break;
                }
                if (iu == 0 && ou == 0) {
                    break; // stuck; cannot happen with a 4KiB buffer
                }
                used += iu;
            }
            off += used;
            if (used < chunk_len) {
                break;
            }
        }
        // Flush the converter at end.
        if (rc == 0) {
            size_t iu = 0;
            size_t ou = 0;
            if (edit_conv_step(&conv, NULL, 0, buf, TBUF_CVT_CHUNK, &iu, &ou) != 0) {
                edit_conv_error(&conv, err);
                rc = -1;
            } else if (ou > 0 && !write_full(fd, buf, ou)) {
                if (err != NULL) {
                    *err = edit_error_sys((uint32_t)errno);
                }
                rc = -1;
            }
        }
        edit_conv_destroy(&conv);
        free(pivot);
        free(buf);
        if (rc != 0) {
            return rc;
        }
    }
    t->save_generation = edit_gap_generation(&t->buffer);
    return 0;
}
