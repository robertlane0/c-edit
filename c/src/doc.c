#include "edit/doc.h"

size_t edit_doc_len(const edit_doc_t *doc) {
    if (doc == NULL || doc->len == NULL) {
        return 0;
    }
    return doc->len(doc->ctx);
}

static size_t slice_len(const void *ctx) {
    const edit_slice_doc_t *s = (const edit_slice_doc_t *)ctx;
    return s == NULL ? 0 : s->len;
}

static void slice_fwd(const void *ctx, size_t off, const uint8_t **out_ptr, size_t *out_len) {
    const edit_slice_doc_t *s = (const edit_slice_doc_t *)ctx;
    if (out_ptr != NULL) {
        *out_ptr = NULL;
    }
    if (out_len != NULL) {
        *out_len = 0;
    }
    if (s == NULL || s->bytes == NULL || out_ptr == NULL || out_len == NULL) {
        return;
    }
    size_t o = off < s->len ? off : s->len;
    *out_ptr = s->bytes + o;
    *out_len = s->len - o;
}

static void slice_bwd(const void *ctx, size_t off, const uint8_t **out_ptr, size_t *out_len) {
    const edit_slice_doc_t *s = (const edit_slice_doc_t *)ctx;
    if (out_ptr != NULL) {
        *out_ptr = NULL;
    }
    if (out_len != NULL) {
        *out_len = 0;
    }
    if (s == NULL || s->bytes == NULL || out_ptr == NULL || out_len == NULL) {
        return;
    }
    size_t o = off < s->len ? off : s->len;
    *out_ptr = s->bytes;
    *out_len = o;
}

void edit_slice_doc_init(edit_slice_doc_t *s, const uint8_t *bytes, size_t len) {
    if (s == NULL) {
        return;
    }
    s->bytes = (len > 0 && bytes == NULL) ? NULL : bytes;
    s->len = (s->bytes == NULL) ? 0 : len;
    s->doc.ctx = s;
    s->doc.len = slice_len;
    s->doc.read_fwd = slice_fwd;
    s->doc.read_bwd = slice_bwd;
    s->doc.replace = NULL;
}
