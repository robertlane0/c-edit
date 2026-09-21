#ifndef EDIT_CELL_H
#define EDIT_CELL_H

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Interior mutability for single-threaded code (cf. Rust SemiRefCell).
// Borrow rules: many shared XOR one exclusive; violations assert in debug.
// Release builds skip tracking (matches Rust release); keep sequences valid.
#define EDIT_CELL_DEFINE(Name, T)                                                                  \
    typedef struct {                                                                               \
        T value;                                                                                   \
        int32_t readers;                                                                           \
        bool writer;                                                                               \
    } Name##_cell_t;                                                                               \
                                                                                                   \
    static inline Name##_cell_t Name##_cell_new(T v) {                                             \
        Name##_cell_t c;                                                                           \
        c.value = v;                                                                               \
        c.readers = 0;                                                                             \
        c.writer = false;                                                                          \
        return c;                                                                                  \
    }                                                                                              \
                                                                                                   \
    static inline const T *Name##_cell_borrow(Name##_cell_t *c) {                                  \
        assert(c != NULL && !c->writer);                                                           \
        if (c == NULL || c->writer) {                                                              \
            return NULL;                                                                           \
        }                                                                                          \
        ++c->readers;                                                                              \
        return &c->value;                                                                          \
    }                                                                                              \
                                                                                                   \
    static inline void Name##_cell_unborrow(Name##_cell_t *c) {                                    \
        assert(c != NULL && c->readers > 0);                                                       \
        if (c == NULL || c->readers <= 0) {                                                        \
            return;                                                                                \
        }                                                                                          \
        --c->readers;                                                                              \
    }                                                                                              \
                                                                                                   \
    static inline T *Name##_cell_borrow_mut(Name##_cell_t *c) {                                    \
        assert(c != NULL && !c->writer && c->readers == 0);                                        \
        if (c == NULL || c->writer || c->readers != 0) {                                           \
            return NULL;                                                                           \
        }                                                                                          \
        c->writer = true;                                                                          \
        return &c->value;                                                                          \
    }                                                                                              \
                                                                                                   \
    static inline void Name##_cell_unborrow_mut(Name##_cell_t *c) {                                \
        assert(c != NULL && c->writer);                                                            \
        if (c == NULL || !c->writer) {                                                             \
            return;                                                                                \
        }                                                                                          \
        c->writer = false;                                                                         \
    }

#ifdef __cplusplus
}
#endif

#endif
