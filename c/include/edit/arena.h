#ifndef EDIT_ARENA_H
#define EDIT_ARENA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EDIT_ARENA_CHUNK ((size_t)65536)

// Bump allocator over lazily-committed VM (cf. Rust release::Arena).
// Single-threaded; no destructors run; deallocate is a no-op.
// Zero-init is a valid empty arena; destroy is safe on it.
typedef struct {
    uint8_t *base;
    size_t capacity;
    size_t commit;
    size_t offset;
#ifndef NDEBUG
    size_t borrows;
#endif
} edit_arena_t;

// Reserve (capacity rounded up to 64 KiB); 0 ok, -1 on error.
int edit_arena_init(edit_arena_t *a, size_t capacity);
void edit_arena_destroy(edit_arena_t *a);
size_t edit_arena_offset(const edit_arena_t *a);
uint8_t *edit_arena_base(edit_arena_t *a);
size_t edit_arena_capacity(const edit_arena_t *a);
// Rewind watermark; caller must pass a prior offset. No destructors run.
void edit_arena_reset(edit_arena_t *a, size_t to);
// Bump-allocate; align must be a nonzero power of two.
bool edit_arena_alloc(edit_arena_t *a, size_t bytes, size_t align, void **out);
bool edit_arena_alloc_zeroed(edit_arena_t *a, size_t bytes, size_t align, void **out);
// Vec-style growth: tail extends in place, else alloc (with align) + copy. No-op free.
bool edit_arena_grow(edit_arena_t *a, void *ptr, size_t old_size, size_t new_size, size_t align,
                     void **out);
// Tail-only shrink; non-tail asserts (debug) and keeps old size.
bool edit_arena_shrink(edit_arena_t *a, void *ptr, size_t old_size, size_t new_size,
                       size_t *out_len);

// Two global flip-flop arenas for temporaries; init once before use.
int edit_scratch_init(size_t capacity);
void edit_scratch_destroy(void);

typedef struct {
    edit_arena_t *arena;
    size_t offset;
#ifndef NDEBUG
    size_t borrow;
#endif
} edit_scratch_t;

// Borrows the arena not conflicting with the argument (pass NULL if none).
edit_scratch_t edit_scratch_acquire(const edit_arena_t *conflict);
// Resets the arena to the acquisition offset.
void edit_scratch_release(edit_scratch_t *s);
// Checked scratch ops: assert most-recent-borrow discipline in debug.
bool edit_scratch_alloc(edit_scratch_t *s, size_t bytes, size_t align, void **out);
size_t edit_scratch_offset(edit_scratch_t *s);

#ifdef __cplusplus
}
#endif

#endif
