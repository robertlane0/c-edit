#include "edit/arena.h"

#include <assert.h>
#include <string.h>

#include "edit/vm.h"

static bool is_pow2(size_t v) {
    return v != 0 && (v & (v - 1)) == 0;
}

static size_t round_chunk(size_t v) {
    return (v + EDIT_ARENA_CHUNK - 1) & ~(EDIT_ARENA_CHUNK - 1);
}

int edit_arena_init(edit_arena_t *a, size_t capacity) {
    if (a == NULL) {
        return -1;
    }
    size_t cap = capacity > 1 ? capacity : 1;
    if (cap > (size_t)(-1) - (EDIT_ARENA_CHUNK - 1)) {
        return -1;
    }
    cap = round_chunk(cap);
    uint8_t *base = NULL;
    edit_error_t err;
    if (!edit_vm_reserve(cap, &base, &err)) {
        (void)err;
        return -1;
    }
    a->base = base;
    a->capacity = cap;
    a->commit = 0;
    a->offset = 0;
#ifndef NDEBUG
    a->borrows = 0;
#endif
    return 0;
}

void edit_arena_destroy(edit_arena_t *a) {
    if (a == NULL) {
        return;
    }
    if (a->base != NULL) {
        edit_vm_release(a->base, a->capacity);
        a->base = NULL;
    }
    a->capacity = 0;
    a->commit = 0;
    a->offset = 0;
}

size_t edit_arena_offset(const edit_arena_t *a) {
    if (a == NULL) {
        return 0;
    }
    return a->offset;
}

uint8_t *edit_arena_base(edit_arena_t *a) {
    if (a == NULL) {
        return NULL;
    }
    return a->base;
}

size_t edit_arena_capacity(const edit_arena_t *a) {
    if (a == NULL) {
        return 0;
    }
    return a->capacity;
}

void edit_arena_reset(edit_arena_t *a, size_t to) {
    if (a == NULL) {
        return;
    }
#ifndef NDEBUG
    if (a->offset > to && a->base != NULL) {
        size_t end = a->offset + 128;
        if (end < a->offset) {
            end = (size_t)(-1); // saturate on overflow
        }
        if (end > a->commit) {
            end = a->commit;
        }
        if (end > to) {
            memset(a->base + to, 0xDD, end - to);
        }
    }
#endif
    a->offset = to;
}

bool edit_arena_alloc(edit_arena_t *a, size_t bytes, size_t align, void **out) {
    if (out == NULL) {
        return false;
    }
    *out = NULL;
    if (a == NULL || a->base == NULL) {
        return false;
    }
    if (!is_pow2(align)) {
        return false;
    }
    if (align - 1 > (size_t)(-1) - a->offset) {
        return false;
    }
    size_t beg = (a->offset + align - 1) & ~(align - 1);
    if (bytes > (size_t)(-1) - beg) {
        return false;
    }
    size_t end = beg + bytes;

    if (end > a->commit) {
        if (end > (size_t)(-1) - (EDIT_ARENA_CHUNK - 1)) {
            return false;
        }
        size_t commit_new = round_chunk(end);
        if (commit_new > a->capacity) {
            return false;
        }
        edit_error_t err;
        if (!edit_vm_commit(a->base + a->commit, commit_new - a->commit, &err)) {
            (void)err;
            return false;
        }
        a->commit = commit_new;
    }

#ifndef NDEBUG
    {
        size_t fill = end + 128;
        if (fill < end) {
            fill = (size_t)(-1);
        }
        if (fill > a->commit) {
            fill = a->commit;
        }
        if (fill > a->offset) {
            memset(a->base + a->offset, 0xCD, fill - a->offset);
        }
    }
#endif

    a->offset = end;
    *out = a->base + beg;
    return true;
}

bool edit_arena_alloc_zeroed(edit_arena_t *a, size_t bytes, size_t align, void **out) {
    if (!edit_arena_alloc(a, bytes, align, out)) {
        return false;
    }
    memset(*out, 0, bytes);
    return true;
}

bool edit_arena_grow(edit_arena_t *a, void *ptr, size_t old_size, size_t new_size, size_t align,
                     void **out) {
    if (out == NULL) {
        return false;
    }
    *out = NULL;
    if (a == NULL || ptr == NULL) {
        return false;
    }
    if (new_size < old_size) {
        assert(new_size >= old_size);
        return false;
    }
    if (old_size > UINTPTR_MAX - (uintptr_t)ptr) {
        return false;
    }
    if ((uint8_t *)ptr < a->base || (uint8_t *)ptr + old_size > a->base + a->offset) {
        return false;
    }
    if ((uint8_t *)ptr + old_size == a->base + a->offset) {
        // Tail: extend in place.
        void *extra = NULL;
        if (!edit_arena_alloc(a, new_size - old_size, 1, &extra)) {
            return false;
        }
        (void)extra;
        *out = ptr;
        return true;
    }
    void *dst = NULL;
    if (!edit_arena_alloc(a, new_size, align, &dst)) {
        return false;
    }
    memcpy(dst, ptr, old_size);
    *out = dst;
    return true;
}

bool edit_arena_shrink(edit_arena_t *a, void *ptr, size_t old_size, size_t new_size,
                       size_t *out_len) {
    if (out_len == NULL) {
        return false;
    }
    if (a == NULL || ptr == NULL) {
        return false;
    }
    if (new_size > old_size) {
        assert(new_size <= old_size);
        return false;
    }
    *out_len = old_size;
    if ((uint8_t *)ptr + old_size == a->base + a->offset) {
        a->offset = a->offset - old_size + new_size;
        *out_len = new_size;
    } else {
        assert(false && "Only the last allocation can be shrunk!");
    }
    return true;
}

static edit_arena_t s_scratch[2];
static bool s_scratch_init = false;

int edit_scratch_init(size_t capacity) {
    for (int i = 0; i < 2; ++i) {
        if (edit_arena_init(&s_scratch[i], capacity) != 0) {
            for (int j = 0; j < i; ++j) {
                edit_arena_destroy(&s_scratch[j]);
            }
            return -1;
        }
    }
    s_scratch_init = true;
    return 0;
}

void edit_scratch_destroy(void) {
    edit_arena_destroy(&s_scratch[0]);
    edit_arena_destroy(&s_scratch[1]);
    s_scratch_init = false;
}

edit_scratch_t edit_scratch_acquire(const edit_arena_t *conflict) {
    size_t index = (conflict == &s_scratch[0]) ? 1 : 0;
    edit_arena_t *arena = &s_scratch[index];
    edit_scratch_t s;
    s.arena = arena;
    s.offset = arena->offset;
#ifndef NDEBUG
    s.borrow = arena->borrows + 1;
    arena->borrows = s.borrow;
#endif
    return s;
}

void edit_scratch_release(edit_scratch_t *s) {
    if (s == NULL || s->arena == NULL) {
        return;
    }
#ifndef NDEBUG
    assert(s->borrow == s->arena->borrows && "Scratch released out of order");
    if (s->borrow == s->arena->borrows) {
        s->arena->borrows -= 1;
    }
#endif
    edit_arena_reset(s->arena, s->offset);
    s->arena = NULL;
}

bool edit_scratch_alloc(edit_scratch_t *s, size_t bytes, size_t align, void **out) {
    if (s == NULL || s->arena == NULL) {
        if (out != NULL) {
            *out = NULL;
        }
        return false;
    }
#ifndef NDEBUG
    assert(s->borrow == s->arena->borrows && "Arena already borrowed by a newer ScratchArena");
    if (s->borrow != s->arena->borrows) {
        if (out != NULL) {
            *out = NULL;
        }
        return false;
    }
#endif
    return edit_arena_alloc(s->arena, bytes, align, out);
}

size_t edit_scratch_offset(edit_scratch_t *s) {
    if (s == NULL || s->arena == NULL) {
        return 0;
    }
#ifndef NDEBUG
    assert(s->borrow == s->arena->borrows && "Arena already borrowed by a newer ScratchArena");
    if (s->borrow != s->arena->borrows) {
        return 0;
    }
#endif
    return s->arena->offset;
}
