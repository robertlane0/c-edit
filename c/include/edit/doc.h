#ifndef EDIT_DOC_H
#define EDIT_DOC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Read/write over text containers (cf. Rust ReadableDocument/WriteableDocument).
// Lenient input: offsets clamp. Strict output: no split graphemes; empty
// fwd chunk only at/beyond end, empty bwd chunk only at 0. Chunks borrow
// the container; do not retain across mutation. Not thread-safe.
typedef struct {
    void *ctx;
    size_t (*len)(const void *ctx);
    void (*read_fwd)(const void *ctx, size_t off, const uint8_t **out_ptr, size_t *out_len);
    void (*read_bwd)(const void *ctx, size_t off, const uint8_t **out_ptr, size_t *out_len);
    // NULL for read-only docs. Clamps range; replacement need not be UTF-8.
    bool (*replace)(void *ctx, size_t beg, size_t end, const uint8_t *src, size_t n);
} edit_doc_t;

size_t edit_doc_len(const edit_doc_t *doc);

// Plain byte-slice doc (cf. ReadableDocument for &[u8]); borrows bytes.
typedef struct {
    edit_doc_t doc;
    const uint8_t *bytes;
    size_t len;
} edit_slice_doc_t;

void edit_slice_doc_init(edit_slice_doc_t *s, const uint8_t *bytes, size_t len);

#ifdef __cplusplus
}
#endif

#endif
