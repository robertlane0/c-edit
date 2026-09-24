#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "edit/arena.h"

static int checks = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #cond);                                     \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static int check_aligned(const void *p, size_t align) {
    ++checks;
    if (((uintptr_t)p & (align - 1)) != 0) {
        fprintf(stderr, "FAIL %d: misaligned %p for %zu\n", __LINE__, p, align);
        return 1;
    }
    return 0;
}

int main(void) {
    // Zero-init is a valid empty arena.
    edit_arena_t z = {0};
    CHECK(edit_arena_offset(&z) == 0);
    CHECK(edit_arena_capacity(&z) == 0);
    CHECK(edit_arena_base(&z) == NULL);
    edit_arena_destroy(&z);

    // Invalid inputs fail gracefully.
    void *nz = (void *)0x1;
    CHECK(edit_arena_init(NULL, 64) != 0);
    CHECK(!edit_arena_alloc(NULL, 8, 1, &nz) && nz == NULL);
    CHECK(!edit_arena_alloc(&z, 8, 1, &nz) && nz == NULL); // uninitialized
    CHECK(!edit_arena_alloc_zeroed(NULL, 8, 1, &nz));
    CHECK(!edit_arena_grow(NULL, nz, 8, 16, 1, &nz));
    size_t zl = 9;
    CHECK(!edit_arena_shrink(NULL, nz, 8, 4, &zl));
    CHECK(edit_arena_offset(NULL) == 0);
    CHECK(edit_arena_capacity(NULL) == 0);
    CHECK(edit_arena_base(NULL) == NULL);
    edit_arena_destroy(NULL);
    edit_arena_reset(NULL, 0);

    // Capacity rounds up to 64 KiB, minimum 1 -> 64 KiB.
    edit_arena_t a = {0};
    CHECK(edit_arena_init(&a, 1) == 0);
    CHECK(edit_arena_capacity(&a) == 65536);
    CHECK(edit_arena_offset(&a) == 0);
    edit_arena_destroy(&a);

    CHECK(edit_arena_init(&a, 70000) == 0);
    CHECK(edit_arena_capacity(&a) == 131072);

    // Bump allocations: aligned, disjoint, monotonic.
    void *p1 = NULL;
    void *p2 = NULL;
    void *p3 = NULL;
    CHECK(edit_arena_alloc(&a, 1, 1, &p1));
    CHECK(edit_arena_alloc(&a, 8, 8, &p2));
    CHECK(edit_arena_alloc(&a, 100, 16, &p3));
    CHECK(check_aligned(p2, 8) == 0);
    CHECK(check_aligned(p3, 16) == 0);
    CHECK(p1 != p2 && p2 != p3);
    CHECK((uint8_t *)p2 >= (uint8_t *)p1 + 1);
    CHECK((uint8_t *)p3 >= (uint8_t *)p2 + 8);
    size_t off = edit_arena_offset(&a);
    CHECK(off > 100);

    // Zeroed allocations read back zero.
    void *pz = NULL;
    CHECK(edit_arena_alloc_zeroed(&a, 64, 8, &pz));
    for (int i = 0; i < 64; ++i) {
        CHECK(((uint8_t *)pz)[i] == 0);
    }

    // Over-capacity and invalid args fail cleanly.
    void *big = NULL;
    CHECK(!edit_arena_alloc(&a, edit_arena_capacity(&a) + 1, 1, &big));
    CHECK(!edit_arena_alloc(&a, (size_t)(-1), 1, &big));
    CHECK(!edit_arena_alloc(&a, 8, 0, &big));
    CHECK(!edit_arena_alloc(&a, 8, 3, &big));
    CHECK(!edit_arena_alloc(NULL, 8, 1, &big));
    CHECK(!edit_arena_alloc(&a, 8, 1, NULL));

    // Reset rewinds the watermark; memory is reusable.
    size_t mark = edit_arena_offset(&a);
    void *pa = NULL;
    CHECK(edit_arena_alloc(&a, 32, 1, &pa));
    memset(pa, 0xAB, 32);
    edit_arena_reset(&a, mark);
    CHECK(edit_arena_offset(&a) == mark);
    void *pb = NULL;
    CHECK(edit_arena_alloc(&a, 32, 1, &pb));
    CHECK(pb == pa); // same region handed out again
#ifndef NDEBUG
    // Debug fill marks freed bytes.
    edit_arena_reset(&a, mark);
    CHECK(edit_arena_base(&a)[mark] == 0xDD);
#endif

    // Tail growth extends in place with data intact.
    void *pg = NULL;
    CHECK(edit_arena_alloc(&a, 8, 8, &pg));
    memset(pg, 0x5A, 8);
    void *pg2 = NULL;
    CHECK(edit_arena_grow(&a, pg, 8, 24, 8, &pg2));
    CHECK(pg2 == pg);
    for (int i = 0; i < 8; ++i) {
        CHECK(((uint8_t *)pg2)[i] == 0x5A);
    }

    // Non-tail growth copies to a new home.
    void *ph = NULL;
    CHECK(edit_arena_alloc(&a, 8, 1, &ph));
    memset(ph, 0xA5, 8);
    void *tail = NULL;
    CHECK(edit_arena_alloc(&a, 8, 1, &tail)); // pushes ph off the tail
    (void)tail;
    void *ph2 = NULL;
    CHECK(edit_arena_grow(&a, ph, 8, 32, 1, &ph2));
    CHECK(ph2 != ph);
    for (int i = 0; i < 8; ++i) {
        CHECK(((uint8_t *)ph2)[i] == 0xA5);
    }

    // Tail shrink rewinds; reported length follows.
    void *ps = NULL;
    CHECK(edit_arena_alloc(&a, 16, 1, &ps));
    size_t got = 0;
    CHECK(edit_arena_shrink(&a, ps, 16, 8, &got));
    CHECK(got == 8);
    void *ps2 = NULL;
    CHECK(edit_arena_alloc(&a, 8, 1, &ps2));
    CHECK(ps2 == (uint8_t *)ps + 8); // shrink freed the tail half

    edit_arena_destroy(&a);
    CHECK(edit_arena_base(&a) == NULL);

    // Scratch arenas: init, flip-flop, reset on release.
    CHECK(edit_scratch_init(65536) == 0);
    edit_arena_t other = {0};
    CHECK(edit_arena_init(&other, 4096) == 0);
    edit_scratch_t s1 = edit_scratch_acquire(NULL);
    void *q1 = NULL;
    CHECK(edit_scratch_alloc(&s1, 64, 1, &q1));
    size_t used = edit_scratch_offset(&s1);
    CHECK(used >= 64);
    // Conflicting arena picks the other scratch slot.
    edit_scratch_t s2 = edit_scratch_acquire(s1.arena);
    CHECK(s2.arena != s1.arena);
    void *q2 = NULL;
    CHECK(edit_scratch_alloc(&s2, 32, 1, &q2));
    edit_scratch_release(&s2);
    CHECK(edit_scratch_offset(&s1) == used); // s1 untouched
    edit_scratch_release(&s1);
    // Re-acquire starts clean.
    edit_scratch_t s3 = edit_scratch_acquire(NULL);
    CHECK(edit_scratch_offset(&s3) == 0);
    edit_scratch_release(&s3);
    edit_arena_destroy(&other);
    edit_scratch_destroy();

    printf("test_arena: %d checks passed\n", checks);
    return 0;
}
