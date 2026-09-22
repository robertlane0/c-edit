#ifndef EDIT_GAP_H
#define EDIT_GAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "edit/doc.h"

#ifdef __cplusplus
extern "C" {
#endif

// Gap buffer over VM (large) or heap (small), cf. Rust buffer::gap_buffer.
// Chunks borrow the buffer; mutations may relocate (small) - re-read after.
// Single-threaded; offsets clamp; generation wraps.
typedef struct {
    uint8_t *text;
    size_t reserve;
    size_t commit;
    size_t text_len;
    size_t gap_off;
    size_t gap_len;
    uint32_t generation;
    bool is_small;
    uint8_t *heap;
} edit_gap_t;

int edit_gap_init(edit_gap_t *g, bool small);
void edit_gap_destroy(edit_gap_t *g);
size_t edit_gap_len(const edit_gap_t *g);
uint32_t edit_gap_generation(const edit_gap_t *g);
void edit_gap_set_generation(edit_gap_t *g, uint32_t gen);
// Writable gap of >= len bytes at off after deleting del bytes there.
// Actual size in *out_len (smaller on OOM); NULL on bad args.
uint8_t *edit_gap_allocate(edit_gap_t *g, size_t off, size_t len, size_t del, size_t *out_len);
// Commits the first len bytes of the allocated gap as text.
void edit_gap_commit(edit_gap_t *g, size_t len);
bool edit_gap_replace(edit_gap_t *g, size_t beg, size_t end, const uint8_t *src, size_t n);
void edit_gap_clear(edit_gap_t *g);
void edit_gap_read_fwd(const edit_gap_t *g, size_t off, const uint8_t **out_ptr, size_t *out_len);
void edit_gap_read_bwd(const edit_gap_t *g, size_t off, const uint8_t **out_ptr, size_t *out_len);
// Copies [beg,end) into out (cap bytes); returns bytes written.
size_t edit_gap_extract(const edit_gap_t *g, size_t beg, size_t end, uint8_t *out, size_t cap);
// Bulk copy via doc interface; copy_from returns true if changed.
bool edit_gap_copy_from(edit_gap_t *g, const edit_doc_t *src);
bool edit_gap_copy_into(const edit_gap_t *g, edit_doc_t *dst);
// Exposes the buffer as a read/write doc.
void edit_gap_doc(edit_gap_t *g, edit_doc_t *doc);

#ifdef __cplusplus
}
#endif

#endif
