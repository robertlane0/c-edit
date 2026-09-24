#include "edit/gap.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "edit/vm.h"

#if UINTPTR_MAX == UINT64_MAX
#define GAP_LARGE_RESERVE ((size_t)4 * 1024 * 1024 * 1024)
#else
#define GAP_LARGE_RESERVE ((size_t)128 * 1024 * 1024)
#endif
#define GAP_LARGE_ALLOC ((size_t)64 * 1024)
#define GAP_LARGE_GAP ((size_t)4 * 1024)

#define GAP_SMALL_RESERVE ((size_t)128 * 1024)
#define GAP_SMALL_ALLOC ((size_t)256)
#define GAP_SMALL_GAP ((size_t)16)

static size_t round_up(size_t v, size_t chunk) {
    return (v + chunk - 1) & ~(chunk - 1);
}

int edit_gap_init(edit_gap_t *g, bool small) {
    if (g == NULL) {
        return -1;
    }
    g->text = NULL;
    g->reserve = small ? GAP_SMALL_RESERVE : GAP_LARGE_RESERVE;
    g->commit = 0;
    g->text_len = 0;
    g->gap_off = 0;
    g->gap_len = 0;
    g->generation = 0;
    g->is_small = small;
    g->heap = NULL;
    if (!small) {
        edit_error_t err;
        if (!edit_vm_reserve(g->reserve, &g->text, &err)) {
            (void)err;
            return -1;
        }
    }
    return 0;
}

void edit_gap_destroy(edit_gap_t *g) {
    if (g == NULL) {
        return;
    }
    if (g->is_small) {
        free(g->heap);
    } else if (g->text != NULL) {
        edit_vm_release(g->text, g->reserve);
    }
    g->text = NULL;
    g->heap = NULL;
    g->commit = 0;
    g->text_len = 0;
    g->gap_off = 0;
    g->gap_len = 0;
}

size_t edit_gap_len(const edit_gap_t *g) {
    return g == NULL ? 0 : g->text_len;
}

uint32_t edit_gap_generation(const edit_gap_t *g) {
    return g == NULL ? 0 : g->generation;
}

void edit_gap_set_generation(edit_gap_t *g, uint32_t gen) {
    if (g != NULL) {
        g->generation = gen;
    }
}

static void gap_move(edit_gap_t *g, size_t off) {
    if (g->gap_len > 0) {
        bool left = off < g->gap_off;
        size_t src = left ? off : g->gap_off + g->gap_len;
        size_t dst = left ? off + g->gap_len : g->gap_off;
        size_t n = left ? g->gap_off - off : off - g->gap_off;
        memmove(g->text + dst, g->text + src, n);
#ifndef NDEBUG
        memset(g->text + off, 0xCD, g->gap_len);
#endif
    }
    g->gap_off = off;
}

static void gap_delete(edit_gap_t *g, size_t n) {
#ifndef NDEBUG
    memset(g->text + g->gap_off + g->gap_len, 0xCD, n);
#endif
    g->gap_len += n;
    g->text_len -= n;
}

static void gap_enlarge(edit_gap_t *g, size_t len) {
    size_t gap_chunk = g->is_small ? GAP_SMALL_GAP : GAP_LARGE_GAP;
    size_t alloc_chunk = g->is_small ? GAP_SMALL_ALLOC : GAP_LARGE_ALLOC;

    if (len > (size_t)(-1) - gap_chunk) {
        return; // saturates: gap stays small, like Rust OOM path
    }
    size_t gap_new = round_up(len + gap_chunk, gap_chunk);
    if (gap_new < len) {
        return;
    }
    if (g->text_len > (size_t)(-1) - gap_new) {
        return;
    }
    size_t bytes_new = g->text_len + gap_new;

    if (bytes_new > g->commit) {
        if (bytes_new > (size_t)(-1) - (alloc_chunk - 1)) {
            return;
        }
        size_t rounded = round_up(bytes_new, alloc_chunk);
        if (rounded < bytes_new || rounded > g->reserve) {
            return;
        }
        if (g->is_small) {
            uint8_t *heap = (uint8_t *)realloc(g->heap, rounded);
            if (heap == NULL) {
                return;
            }
            g->heap = heap;
            g->text = heap;
        } else {
            edit_error_t err;
            if (!edit_vm_commit(g->text + g->commit, rounded - g->commit, &err)) {
                (void)err;
                return;
            }
        }
        g->commit = rounded;
    }

    // Shift the tail right to open the larger gap (may overlap).
    memmove(g->text + g->gap_off + gap_new, g->text + g->gap_off + g->gap_len,
            g->text_len - g->gap_off);
#ifndef NDEBUG
    memset(g->text + g->gap_off + g->gap_len, 0xCD, gap_new - g->gap_len);
#endif
    g->gap_len = gap_new;
}

uint8_t *edit_gap_allocate(edit_gap_t *g, size_t off, size_t len, size_t del, size_t *out_len) {
    if (out_len != NULL) {
        *out_len = 0;
    }
    if (g == NULL || out_len == NULL) {
        return NULL;
    }
    if (!g->is_small && g->text == NULL) {
        return NULL;
    }
    if (off > g->text_len) {
        off = g->text_len;
    }
    if (del > g->text_len - off) {
        del = g->text_len - off;
    }
    if (off != g->gap_off) {
        gap_move(g, off);
    }
    if (del > 0) {
        gap_delete(g, del);
    }
    if (len > g->gap_len) {
        gap_enlarge(g, len);
    }
    g->generation += 1; // wraps, like Rust wrapping_add
    *out_len = g->gap_len;
    return g->text == NULL ? NULL : g->text + g->gap_off;
}

void edit_gap_commit(edit_gap_t *g, size_t len) {
    assert(g != NULL && len <= g->gap_len);
    if (g == NULL || len > g->gap_len) {
        return;
    }
    g->text_len += len;
    g->gap_off += len;
    g->gap_len -= len;
}

bool edit_gap_replace(edit_gap_t *g, size_t beg, size_t end, const uint8_t *src, size_t n) {
    if (g == NULL) {
        return false;
    }
    if (n > 0 && src == NULL) {
        return false;
    }
    size_t del = end > beg ? end - beg : 0;
    size_t gap = 0;
    uint8_t *dst = edit_gap_allocate(g, beg, n, del, &gap);
    size_t k = n < gap ? n : gap;
    if (k > 0) {
        if (dst == NULL) {
            return false;
        }
        memcpy(dst, src, k);
    }
    edit_gap_commit(g, k);
    return true;
}

void edit_gap_clear(edit_gap_t *g) {
    if (g == NULL) {
        return;
    }
    g->gap_off = 0;
    g->gap_len += g->text_len;
    g->generation += 1;
    g->text_len = 0;
}

void edit_gap_read_fwd(const edit_gap_t *g, size_t off, const uint8_t **out_ptr, size_t *out_len) {
    if (out_ptr != NULL) {
        *out_ptr = NULL;
    }
    if (out_len != NULL) {
        *out_len = 0;
    }
    if (g == NULL || g->text == NULL || out_ptr == NULL || out_len == NULL) {
        return;
    }
    if (off > g->text_len) {
        off = g->text_len;
    }
    size_t beg;
    size_t len;
    if (off < g->gap_off) {
        beg = off;
        len = g->gap_off - off;
    } else {
        beg = off + g->gap_len;
        len = g->text_len - off;
    }
    *out_ptr = g->text + beg;
    *out_len = len;
}

void edit_gap_read_bwd(const edit_gap_t *g, size_t off, const uint8_t **out_ptr, size_t *out_len) {
    if (out_ptr != NULL) {
        *out_ptr = NULL;
    }
    if (out_len != NULL) {
        *out_len = 0;
    }
    if (g == NULL || g->text == NULL || out_ptr == NULL || out_len == NULL) {
        return;
    }
    if (off > g->text_len) {
        off = g->text_len;
    }
    size_t beg;
    size_t len;
    if (off <= g->gap_off) {
        beg = 0;
        len = off;
    } else {
        beg = g->gap_off + g->gap_len;
        len = off - g->gap_off;
    }
    *out_ptr = g->text + beg;
    *out_len = len;
}

size_t edit_gap_extract(const edit_gap_t *g, size_t beg, size_t end, uint8_t *out, size_t cap) {
    if (g == NULL || g->text == NULL) {
        return 0;
    }
    if (end > g->text_len) {
        end = g->text_len;
    }
    if (beg > end) {
        beg = end;
    }
    if (out == NULL || cap == 0 || beg >= end) {
        return 0;
    }
    size_t total = 0;
    while (beg < end && total < cap) {
        const uint8_t *chunk = NULL;
        size_t chunk_len = 0;
        edit_gap_read_fwd(g, beg, &chunk, &chunk_len);
        if (chunk == NULL || chunk_len == 0) {
            break;
        }
        size_t want = end - beg;
        size_t k = chunk_len < want ? chunk_len : want;
        if (k > cap - total) {
            k = cap - total;
        }
        memcpy(out + total, chunk, k);
        beg += k;
        total += k;
    }
    return total;
}

static size_t gap_doc_len(const void *ctx) {
    return edit_gap_len((const edit_gap_t *)ctx);
}

static void gap_doc_fwd(const void *ctx, size_t off, const uint8_t **p, size_t *n) {
    edit_gap_read_fwd((const edit_gap_t *)ctx, off, p, n);
}

static void gap_doc_bwd(const void *ctx, size_t off, const uint8_t **p, size_t *n) {
    edit_gap_read_bwd((const edit_gap_t *)ctx, off, p, n);
}

static bool gap_doc_replace(void *ctx, size_t beg, size_t end, const uint8_t *src, size_t n) {
    return edit_gap_replace((edit_gap_t *)ctx, beg, end, src, n);
}

void edit_gap_doc(edit_gap_t *g, edit_doc_t *doc) {
    if (doc == NULL) {
        return;
    }
    doc->ctx = g;
    doc->len = gap_doc_len;
    doc->read_fwd = gap_doc_fwd;
    doc->read_bwd = gap_doc_bwd;
    doc->replace = gap_doc_replace;
}

bool edit_gap_copy_from(edit_gap_t *g, const edit_doc_t *src) {
    if (g == NULL || src == NULL || src->read_fwd == NULL) {
        return false;
    }
    size_t off = 0;
    for (;;) {
        const uint8_t *dchunk = NULL;
        size_t dlen = 0;
        const uint8_t *schunk = NULL;
        size_t slen = 0;
        edit_gap_read_fwd(g, off, &dchunk, &dlen);
        src->read_fwd(src->ctx, off, &schunk, &slen);
        if (dchunk == NULL) {
            dlen = 0;
        }
        if (schunk == NULL) {
            slen = 0;
        }
        size_t len = dlen < slen ? dlen : slen;
        bool mismatch = len > 0 && memcmp(dchunk, schunk, len) != 0;
        if (mismatch) {
            break;
        }
        if (len == 0) {
            if (dlen == slen) {
                return false; // identical
            }
            break;
        }
        off += len;
    }
    for (;;) {
        const uint8_t *chunk = NULL;
        size_t chunk_len = 0;
        src->read_fwd(src->ctx, off, &chunk, &chunk_len);
        if (chunk == NULL) {
            chunk_len = 0;
        }
        if (!edit_gap_replace(g, off, (size_t)(-1), chunk, chunk_len)) {
            return true; // changed (best effort on OOM)
        }
        off += chunk_len;
        if (chunk_len == 0) {
            return true;
        }
    }
}

bool edit_gap_copy_into(const edit_gap_t *g, edit_doc_t *dst) {
    if (g == NULL || dst == NULL || dst->replace == NULL) {
        return false;
    }
    size_t beg = 0;
    size_t off = 0;
    while (off < edit_gap_len(g)) {
        const uint8_t *chunk = NULL;
        size_t chunk_len = 0;
        edit_gap_read_fwd(g, off, &chunk, &chunk_len);
        if (chunk == NULL || chunk_len == 0) {
            break;
        }
        if (!dst->replace(dst->ctx, beg, (size_t)(-1), chunk, chunk_len)) {
            return false;
        }
        beg = (size_t)(-1);
        off += chunk_len;
    }
    // Empty source still clears the destination once (mirrors Rust).
    if (off == 0) {
        dst->replace(dst->ctx, 0, (size_t)(-1), NULL, 0);
    }
    return true;
}
