#include "edit/fuzzy.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "edit/icu.h"
#include "edit/utf8.h"

#define FUZZY_NO_MATCH 0

// Decode bytes to codepoints; invalid sequences become FFFD.
static uint32_t *decode(const uint8_t *s, size_t n, size_t *out_count) {
    uint32_t *cp = (uint32_t *)malloc((n + 1) * sizeof *cp);
    if (cp == NULL) {
        return NULL;
    }
    edit_utf8_chars_t it;
    edit_utf8_chars_init(&it, s, n, 0);
    size_t k = 0;
    uint32_t c = 0;
    while (edit_utf8_next(&it, &c)) {
        cp[k++] = c;
    }
    *out_count = k;
    return cp;
}

// Fold to a fresh NUL-terminated buffer; NULL on failure.
static char *fold(const char *s, size_t n, size_t *out_len) {
    size_t need = edit_fold_case(NULL, 0, s, n);
    if (need == (size_t)(-1)) {
        return NULL;
    }
    char *buf = (char *)malloc(need + 1);
    if (buf == NULL) {
        return NULL;
    }
    size_t got = edit_fold_case(buf, need + 1, s, n);
    if (got != need) {
        free(buf);
        return NULL;
    }
    *out_len = need;
    return buf;
}

static bool consider_equal(uint32_t a, uint32_t b) {
    // Ported 1:1, parens added: separators match across platforms.
    return a == b || a == '/' || (a == '\\' && b == '/') || b == '\\';
}

static int32_t separator_bonus(uint32_t ch) {
    switch (ch) {
    case '/':
    case '\\':
        return 5;
    case '_':
    case '-':
    case '.':
    case ' ':
    case '\'':
    case '"':
    case ':':
        return 4;
    default:
        return 0;
    }
}

static int32_t char_score(uint32_t q, uint32_t ql, bool has_prev, uint32_t tprev, uint32_t tc,
                          uint32_t tcl, int32_t seq) {
    if (!consider_equal(ql, tcl)) {
        return 0;
    }
    int64_t score = 1;
    if (seq > 0) {
        score += (int64_t)seq * 5;
    }
    if (q == tc) {
        score += 1;
    }
    if (!has_prev) {
        score += 8; // start of word
    } else {
        int32_t sep = separator_bonus(tprev);
        if (sep > 0) {
            score += sep;
        } else if (tc != tcl && seq == 0) {
            score += 2; // camel hump
        }
    }
    if (score > INT32_MAX) {
        score = INT32_MAX;
    }
    return (int32_t)score;
}

int edit_score_fuzzy(edit_arena_t *arena, const uint8_t *hay, size_t haylen, const uint8_t *ndl,
                     size_t ndllen, bool allow_noncontig, int32_t *out_score, size_t **out_pos,
                     size_t *out_n) {
    if (out_score == NULL || out_pos == NULL || out_n == NULL) {
        return -1;
    }
    *out_score = FUZZY_NO_MATCH;
    *out_pos = NULL;
    *out_n = 0;
    if (arena == NULL) {
        return -1;
    }
    if ((haylen > 0 && hay == NULL) || (ndllen > 0 && ndl == NULL)) {
        return -1;
    }
    if (haylen == 0 || ndllen == 0) {
        return 0;
    }

    uint32_t *target = NULL;
    uint32_t *query = NULL;
    size_t tlen = 0;
    size_t qlen = 0;
    char *tfold = NULL;
    char *qfold = NULL;
    size_t tflen = 0;
    size_t qflen = 0;
    uint32_t *tl = NULL;
    uint32_t *ql = NULL;
    size_t tllen = 0;
    size_t qllen = 0;
    int32_t *scores = NULL;
    int32_t *matches = NULL;
    int rc = 0;

    target = decode(hay, haylen, &tlen);
    query = decode(ndl, ndllen, &qlen);
    if (target == NULL || query == NULL) {
        goto done;
    }
    if (tlen == 0 || qlen == 0) {
        goto done; // unreachable for nonempty input; guards division below
    }
    if (tlen < qlen) {
        goto done; // query cannot be contained in target
    }
    if (tlen > INT32_MAX || qlen > INT32_MAX) {
        goto done; // keeps i32 matrix math overflow-free
    }
    tfold = fold((const char *)hay, haylen, &tflen);
    qfold = fold((const char *)ndl, ndllen, &qflen);
    if (tfold == NULL || qfold == NULL) {
        rc = -1;
        goto done;
    }
    tl = decode((const uint8_t *)tfold, tflen, &tllen);
    ql = decode((const uint8_t *)qfold, qflen, &qllen);
    if (tl == NULL || ql == NULL) {
        rc = -1;
        goto done;
    }
    // Folded arrays pair by raw index; clamp for safety (Rust assumes
    // folding never shrinks, which holds for real inputs).
    if (qlen == 0 || tlen > (size_t)(-1) / qlen / sizeof *scores) {
        goto done;
    }
    size_t area = tlen * qlen;
    scores = (int32_t *)calloc(area, sizeof *scores);
    matches = (int32_t *)calloc(area, sizeof *matches);
    if (scores == NULL || matches == NULL) {
        goto done;
    }

    for (size_t qi = 0; qi < qlen; ++qi) {
        size_t qoff = qi * tlen;
        size_t qprev = qi > 0 ? (qi - 1) * tlen : 0;
        uint32_t qc = query[qi];
        uint32_t qlc = qi < qllen ? ql[qi] : qc;
        // Contiguous-match fast check: folded query is a prefix here.
        for (size_t ti = 0; ti < tlen; ++ti) {
            size_t cur = qoff + ti;
            int32_t left = ti > 0 ? scores[cur - 1] : 0;
            int32_t diag = 0;
            int32_t seq = 0;
            if (qi > 0 && ti > 0) {
                size_t d = qprev + ti - 1;
                diag = scores[d];
                seq = matches[d];
            }
            int32_t sc = 0;
            if (diag != 0 || qi == 0) {
                bool has_prev = ti != 0;
                uint32_t tprev = has_prev ? target[ti - 1] : 0;
                uint32_t tc = target[ti];
                uint32_t tlc = ti < tllen ? tl[ti] : tc;
                sc = char_score(qc, qlc, has_prev, tprev, tc, tlc, seq);
            }
            bool contiguous = allow_noncontig || qi > 0;
            if (!contiguous && ti < tllen) {
                // target_lower[ti..] starts with query_lower?
                contiguous = qllen <= tllen - ti;
                for (size_t k = 0; contiguous && k < qllen; ++k) {
                    if (tl[ti + k] != ql[k]) {
                        contiguous = false;
                    }
                }
            }
            int64_t total = (int64_t)diag + sc;
            if (sc != 0 && total >= left && contiguous) {
                matches[cur] = seq + 1;
                scores[cur] = total > INT32_MAX ? INT32_MAX : (int32_t)total;
            } else {
                matches[cur] = FUZZY_NO_MATCH;
                scores[cur] = left;
            }
        }
    }

    int32_t final = scores[area - 1];
    // Backtrack from bottom-right; at most qlen positions.
    void *mem = NULL;
    if (!edit_arena_alloc(arena, qlen * sizeof(size_t), _Alignof(size_t), &mem)) {
        goto done;
    }
    size_t *pos = (size_t *)mem;
    size_t npos = 0;
    if (qlen > 0 && tlen > 0) {
        size_t qi = qlen - 1;
        size_t ti = tlen - 1;
        for (;;) {
            size_t cur = qi * tlen + ti;
            if (matches[cur] == FUZZY_NO_MATCH) {
                if (ti == 0) {
                    break;
                }
                --ti;
            } else {
                if (npos < qlen) {
                    pos[npos++] = ti;
                }
                if (qi == 0 || ti == 0) {
                    break;
                }
                --qi;
                --ti;
            }
        }
        // Reverse into place.
        for (size_t i = 0; i < npos / 2; ++i) {
            size_t t = pos[i];
            pos[i] = pos[npos - 1 - i];
            pos[npos - 1 - i] = t;
        }
    }
    *out_score = final;
    *out_pos = npos > 0 ? pos : NULL;
    *out_n = npos;

done:
    free(target);
    free(query);
    free(tfold);
    free(qfold);
    free(tl);
    free(ql);
    free(scores);
    free(matches);
    if (rc != 0) {
        *out_score = FUZZY_NO_MATCH;
        *out_pos = NULL;
        *out_n = 0;
    }
    return rc;
}
