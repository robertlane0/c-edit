#ifndef EDIT_FUZZY_H
#define EDIT_FUZZY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "edit/arena.h"

#ifdef __cplusplus
extern "C" {
#endif

// VS Code-style fuzzy scorer (cf. uncompiled Rust fuzzy.rs, validated
// differentially). Score 0 = no match. Positions index target characters.
// Temps are heap-owned internally; positions live in the caller arena.
// 0 ok, -1 bad args. Oversize inputs (>INT32_MAX chars) score 0.
int edit_score_fuzzy(edit_arena_t *arena, const uint8_t *hay, size_t haylen, const uint8_t *ndl,
                     size_t ndllen, bool allow_noncontig, int32_t *out_score, size_t **out_pos,
                     size_t *out_n);

#ifdef __cplusplus
}
#endif

#endif
