#ifndef EDIT_AVEC_H
#define EDIT_AVEC_H

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "edit/arena.h"

#ifdef __cplusplus
extern "C" {
#endif

// Growable array over an arena for trivially-copyable T (cf. Vec<T, &Arena>).
// No destructors run; deallocate/shrink-off-tail are no-ops or tail-only.
// Growth doubles (min 8 elems); replace_range clamps like Rust vec_replace.
#define EDIT_AVEC_DEFINE(Name, T)                                                                  \
    typedef struct {                                                                               \
        T *data;                                                                                   \
        size_t len;                                                                                \
        size_t cap;                                                                                \
        edit_arena_t *arena;                                                                       \
    } Name##_t;                                                                                    \
                                                                                                   \
    static inline void Name##_init(Name##_t *v, edit_arena_t *arena) {                             \
        v->data = NULL;                                                                            \
        v->len = 0;                                                                                \
        v->cap = 0;                                                                                \
        v->arena = arena;                                                                          \
    }                                                                                              \
                                                                                                   \
    static inline size_t Name##_len(const Name##_t *v) {                                           \
        return v == NULL ? 0 : v->len;                                                             \
    }                                                                                              \
                                                                                                   \
    static inline size_t Name##_capacity(const Name##_t *v) {                                      \
        return v == NULL ? 0 : v->cap;                                                             \
    }                                                                                              \
                                                                                                   \
    static inline bool Name##_reserve(Name##_t *v, size_t additional) {                            \
        if (v == NULL || v->arena == NULL) {                                                       \
            return false;                                                                          \
        }                                                                                          \
        if (additional > (size_t)-1 - v->len) {                                                    \
            return false;                                                                          \
        }                                                                                          \
        size_t need = v->len + additional;                                                         \
        if (need <= v->cap) {                                                                      \
            return true;                                                                           \
        }                                                                                          \
        size_t grown = v->cap != 0 ? v->cap : 8;                                                   \
        while (grown < need) {                                                                     \
            if (grown > (size_t)-1 / 2) {                                                          \
                grown = need;                                                                      \
                break;                                                                             \
            }                                                                                      \
            grown *= 2;                                                                            \
        }                                                                                          \
        if (grown > (size_t)-1 / sizeof(T)) {                                                      \
            return false;                                                                          \
        }                                                                                          \
        void *dst = NULL;                                                                          \
        if (v->data == NULL) {                                                                     \
            if (!edit_arena_alloc(v->arena, grown * sizeof(T), _Alignof(T), &dst)) {               \
                return false;                                                                      \
            }                                                                                      \
        } else {                                                                                   \
            if (!edit_arena_grow(v->arena, v->data, v->len * sizeof(T), grown * sizeof(T),         \
                                 _Alignof(T), &dst)) {                                             \
                return false;                                                                      \
            }                                                                                      \
        }                                                                                          \
        v->data = (T *)dst;                                                                        \
        v->cap = grown;                                                                            \
        return true;                                                                               \
    }                                                                                              \
                                                                                                   \
    static inline bool Name##_push(Name##_t *v, T val) {                                           \
        if (!Name##_reserve(v, 1)) {                                                               \
            return false;                                                                          \
        }                                                                                          \
        v->data[v->len++] = val;                                                                   \
        return true;                                                                               \
    }                                                                                              \
                                                                                                   \
    static inline bool Name##_extend(Name##_t *v, const T *src, size_t n) {                        \
        if (n == 0) {                                                                              \
            return v != NULL;                                                                      \
        }                                                                                          \
        if (v == NULL || src == NULL) {                                                            \
            return false;                                                                          \
        }                                                                                          \
        if (!Name##_reserve(v, n)) {                                                               \
            return false;                                                                          \
        }                                                                                          \
        memcpy(v->data + v->len, src, n * sizeof(T));                                              \
        v->len += n;                                                                               \
        return true;                                                                               \
    }                                                                                              \
                                                                                                   \
    static inline void Name##_clear(Name##_t *v) {                                                 \
        if (v != NULL) {                                                                           \
            v->len = 0;                                                                            \
        }                                                                                          \
    }                                                                                              \
                                                                                                   \
    static inline bool Name##_shrink_to_fit(Name##_t *v) {                                         \
        if (v == NULL || v->arena == NULL) {                                                       \
            return false;                                                                          \
        }                                                                                          \
        if (v->len == v->cap) {                                                                    \
            return true;                                                                           \
        }                                                                                          \
        if (v->data == NULL) {                                                                     \
            return true;                                                                           \
        }                                                                                          \
        size_t got = v->cap;                                                                       \
        if (!edit_arena_shrink(v->arena, v->data, v->cap * sizeof(T), v->len * sizeof(T), &got)) { \
            return false;                                                                          \
        }                                                                                          \
        v->cap = got / sizeof(T);                                                                  \
        return true;                                                                               \
    }                                                                                              \
                                                                                                   \
    static inline bool Name##_replace_range(Name##_t *v, size_t beg, size_t end, const T *src,     \
                                            size_t n) {                                            \
        if (v == NULL) {                                                                           \
            return false;                                                                          \
        }                                                                                          \
        if (n > 0 && src == NULL) {                                                                \
            return false;                                                                          \
        }                                                                                          \
        size_t off = beg < v->len ? beg : v->len;                                                  \
        size_t dl = end > off ? end - off : 0;                                                     \
        if (dl > v->len - off) {                                                                   \
            dl = v->len - off;                                                                     \
        }                                                                                          \
        size_t tail = v->len - off - dl;                                                           \
        if (n > dl && !Name##_reserve(v, n - dl)) {                                                \
            return false;                                                                          \
        }                                                                                          \
        if (tail > 0 && n != dl) {                                                                 \
            memmove(v->data + off + n, v->data + off + dl, tail * sizeof(T));                      \
        }                                                                                          \
        if (n > 0) {                                                                               \
            memcpy(v->data + off, src, n * sizeof(T));                                             \
        }                                                                                          \
        v->len = v->len - dl + n;                                                                  \
        return true;                                                                               \
    }

#ifdef __cplusplus
}
#endif

#endif
