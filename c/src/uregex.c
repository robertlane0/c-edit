#include "edit/uregex.h"

#include <stdlib.h>
#include <string.h>

#include "edit/utf8.h"
#include "icu_priv.h"

#define UCACHE_SIZE 64
#define UCACHE_LIMIT (64 - 4)

// Mirrors ICU UText layout (utext.h); field order verified.
typedef icu_utext_s utext_t;
typedef icu_ufuncs_s ufuncs_t;

typedef struct {
    uint16_t utf16[UCACHE_SIZE];
    uint16_t to_utf8[UCACHE_SIZE];
    uint16_t to_utf16[UCACHE_SIZE];
    size_t utf16_len;
    size_t ascii_len;
    size_t start;
    size_t end;
} ucache_t;

typedef struct {
    ucache_t cache[2];
    bool mru;
} udcache_t;

static const edit_usrc_t *utext_src(const utext_t *ut) {
    return (const edit_usrc_t *)ut->context;
}

static udcache_t *utext_cache(const utext_t *ut) {
    return (udcache_t *)ut->p_extra;
}

static size_t src_len(const edit_usrc_t *s) {
    if (s == NULL || s->doc == NULL) {
        return 0;
    }
    return edit_doc_len(s->doc);
}

static uint32_t src_generation(const edit_usrc_t *s) {
    if (s == NULL || s->generation == NULL) {
        return 0;
    }
    return s->generation(s->gen_ctx);
}

static bool src_dirty(const edit_usrc_t *s, const utext_t *ut, udcache_t *dc) {
    if (s == NULL || s->generation == NULL) {
        return true;
    }
    uint32_t gen = s->generation(s->gen_ctx);
    if ((int64_t)gen != ut->a) {
        dc->cache[0].utf16_len = 0;
        dc->cache[1].utf16_len = 0;
        dc->cache[0].start = 0;
        dc->cache[0].end = 0;
        dc->cache[1].start = 0;
        dc->cache[1].end = 0;
        ((utext_t *)ut)->a = (int64_t)gen;
        return true;
    }
    return false;
}

// Translates [start, start+limit) to UTF-16 with offset maps. Bounds mirror
// Rust utext_access_impl: writes stay within the 64-entry tables.
static void cache_fill(ucache_t *cache, const edit_usrc_t *src, size_t start, size_t limit) {
    size_t u16len = 0;
    size_t u8len = 0;
    size_t ascii = 0;
    if (limit == 0) {
        goto done;
    }
    for (;;) {
        const uint8_t *ptr = NULL;
        size_t avail = 0;
        if (src != NULL && src->doc != NULL && src->doc->read_fwd != NULL) {
            src->doc->read_fwd(src->doc->ctx, start + u8len, &ptr, &avail);
        }
        if (ptr == NULL || avail == 0) {
            break;
        }
        size_t initial = u8len;
        edit_utf8_chars_t it;
        edit_utf8_chars_init(&it, ptr, avail, 0);
        if (u16len == ascii) {
            size_t h = avail < limit - ascii ? avail : limit - ascii;
            size_t k = 0;
            while (k < h && ptr[k] < 0x80) {
                ++k;
            }
            for (size_t i = 0; i < k; ++i) {
                cache->utf16[ascii] = ptr[i];
                cache->to_utf8[ascii] = (uint16_t)ascii;
                cache->to_utf16[ascii] = (uint16_t)ascii;
                ascii += 1;
            }
            u16len += k;
            u8len += k;
            edit_utf8_seek(&it, k);
            if (ascii >= UCACHE_LIMIT) {
                break;
            }
        }
        uint32_t cp = 0;
        bool stop = false;
        while (edit_utf8_next(&it, &cp)) {
            size_t beg = u8len;
            size_t target = initial + edit_utf8_offset(&it);
            while (u8len < target) {
                cache->to_utf16[u8len] = (uint16_t)u16len;
                u8len += 1;
            }
            if (cp <= 0xFFFFU) {
                cache->utf16[u16len] = (uint16_t)cp;
                cache->to_utf8[u16len] = (uint16_t)beg;
                u16len += 1;
            } else {
                uint32_t v = cp - 0x10000U;
                cache->utf16[u16len] = (uint16_t)(v >> 10U) | 0xD800U;
                cache->utf16[u16len + 1] = (uint16_t)(v & 0x3FFU) | 0xDC00U;
                cache->to_utf8[u16len] = (uint16_t)beg;
                cache->to_utf8[u16len + 1] = (uint16_t)beg;
                u16len += 2;
            }
            if (u16len >= UCACHE_LIMIT || u8len >= limit) {
                stop = true;
                break;
            }
        }
        if (stop) {
            break;
        }
    }
done:
    cache->to_utf8[u16len] = (uint16_t)u8len;
    cache->to_utf16[u8len] = (uint16_t)u16len;
    cache->utf16_len = u16len;
    cache->ascii_len = ascii;
    cache->start = start;
    cache->end = start + u8len;
}

static ucache_t *access_impl(utext_t *ut, int64_t native_index, bool forward) {
    const edit_usrc_t *src = utext_src(ut);
    udcache_t *dc = utext_cache(ut);
    size_t text_len = src_len(src);
    int64_t contained = forward ? native_index : native_index - 1;
    if (contained < 0 || (uint64_t)contained >= text_len) {
        return NULL;
    }
    size_t idx = (size_t)contained;
    size_t nidx = (size_t)native_index;
    if (!src_dirty(src, ut, dc)) {
        for (size_t i = 0; i < 2; ++i) {
            ucache_t *c = &dc->cache[i];
            if (c->utf16_len > 0 && idx >= c->start && idx < c->end) {
                dc->mru = i != 0;
                return c;
            }
        }
    }
    // Empty ranges never hit above (utf16_len == 0); matches Rust's
    // range-contains check on 0..0 (contains nothing).
    dc->mru = !dc->mru;
    ucache_t *cache = &dc->cache[dc->mru ? 1 : 0];
    size_t limit;
    size_t start;
    if (forward) {
        limit = text_len - nidx;
        if (limit > UCACHE_LIMIT) {
            limit = UCACHE_LIMIT;
        }
        start = nidx;
    } else {
        limit = nidx < UCACHE_LIMIT ? nidx : UCACHE_LIMIT;
        // Align to a lead byte (skip trail bytes 10xxxxxx).
        size_t beg = nidx - limit;
        const uint8_t *ptr = NULL;
        size_t avail = 0;
        if (src != NULL && src->doc != NULL && src->doc->read_fwd != NULL) {
            src->doc->read_fwd(src->doc->ctx, beg, &ptr, &avail);
        }
        size_t skip = 0;
        while (ptr != NULL && skip < avail && (ptr[skip] & 0xC0U) == 0x80U) {
            ++skip;
        }
        start = beg + skip;
    }
    cache_fill(cache, src, start, limit);
    return cache;
}

static utext_t *utext_clone(utext_t *dest, const utext_t *src, bool deep, int32_t *status) {
    if (status == NULL || *status > 0) {
        return NULL;
    }
    if (deep) {
        *status = 16; // U_UNSUPPORTED_ERROR
        return NULL;
    }
    if (!icu_full_load()) {
        *status = 2; // U_MISSING_RESOURCE_ERROR-ish
        return NULL;
    }
    utext_t *ut = icu_utextsetup(dest, (int32_t)sizeof(udcache_t), status);
    if (ut == NULL || *status > 0) {
        return NULL;
    }
    const udcache_t *sc = utext_cache(src);
    udcache_t *dc = utext_cache(ut);
    const ucache_t *srcc = &sc->cache[sc->mru ? 1 : 0];
    ucache_t *dstc = &dc->cache[dc->mru ? 1 : 0];
    ut->provider_properties = src->provider_properties;
    ut->chunk_native_limit = src->chunk_native_limit;
    ut->native_indexing_limit = src->native_indexing_limit;
    ut->chunk_native_start = src->chunk_native_start;
    ut->chunk_offset = src->chunk_offset;
    ut->chunk_length = src->chunk_length;
    ut->chunk_contents = dstc->utf16;
    ut->p_funcs = src->p_funcs;
    ut->context = src->context;
    ut->a = src->a;
    memcpy(dstc, srcc, sizeof *dstc);
    return ut;
}

static int64_t utext_native_length(utext_t *ut) {
    return (int64_t)src_len(utext_src(ut));
}

static bool utext_access(utext_t *ut, int64_t native_index, bool forward) {
    ucache_t *cache = access_impl(ut, native_index, forward);
    if (cache == NULL) {
        return false;
    }
    if (native_index < 0 || (uint64_t)native_index < cache->start) {
        return false;
    }
    size_t rel = (size_t)((uint64_t)native_index - cache->start);
    if (rel >= UCACHE_SIZE) {
        return false;
    }
    ut->chunk_contents = cache->utf16;
    ut->chunk_length = (int32_t)cache->utf16_len;
    ut->chunk_offset = (int32_t)cache->to_utf16[rel];
    ut->chunk_native_start = (int64_t)cache->start;
    ut->chunk_native_limit = (int64_t)cache->end;
    ut->native_indexing_limit = (int32_t)cache->ascii_len;
    return true;
}

static int64_t utext_map_offset(const utext_t *ut) {
    udcache_t *dc = utext_cache(ut);
    ucache_t *cache = &dc->cache[dc->mru ? 1 : 0];
    if (ut->chunk_offset < 0 || (size_t)ut->chunk_offset >= UCACHE_SIZE) {
        return (int64_t)cache->start;
    }
    size_t rel = cache->to_utf8[(size_t)ut->chunk_offset];
    return (int64_t)(cache->start + rel);
}

static int32_t utext_map_index(const utext_t *ut, int64_t native_index) {
    udcache_t *dc = utext_cache(ut);
    ucache_t *cache = &dc->cache[dc->mru ? 1 : 0];
    if (native_index < ut->chunk_native_start) {
        return 0;
    }
    size_t rel = (size_t)((uint64_t)native_index - (uint64_t)ut->chunk_native_start);
    if (rel >= UCACHE_SIZE) {
        return (int32_t)cache->utf16_len;
    }
    return (int32_t)cache->to_utf16[rel];
}

static const ufuncs_t U_FUNCS = {
    (int32_t)sizeof(ufuncs_t),
    0,
    0,
    0,
    utext_clone,
    utext_native_length,
    utext_access,
    NULL,
    NULL,
    NULL,
    utext_map_offset,
    utext_map_index,
    NULL,
    NULL,
    NULL,
    NULL,
};

int edit_utext_init(edit_utext_t *t, const edit_usrc_t *src) {
    if (t == NULL || src == NULL) {
        return -1;
    }
    t->ut = NULL;
    memset(&t->src, 0, sizeof t->src);
    if (!icu_full_load()) {
        return -1;
    }
    int32_t status = 0;
    utext_t *ut = icu_utextsetup(NULL, (int32_t)sizeof(udcache_t), &status);
    if (ut == NULL || status > 0) {
        return -1;
    }
    t->src = *src;
    ut->p_funcs = &U_FUNCS;
    ut->context = &t->src;
    ut->a = (int64_t)src_generation(&t->src);
    if (!utext_access(ut, 0, true) && src_len(&t->src) > 0) {
        // Nonempty text must yield an initial chunk (mirrors Rust).
        icu_utextclose(ut);
        memset(&t->src, 0, sizeof t->src);
        return -1;
    }
    t->ut = ut;
    return 0;
}

void edit_utext_destroy(edit_utext_t *t) {
    if (t == NULL || t->ut == NULL) {
        return;
    }
    icu_utextclose((utext_t *)t->ut);
    t->ut = NULL;
    memset(&t->src, 0, sizeof t->src);
}

// UTF-8 pattern to UTF-16 with surrogate splits.
static uint16_t *pattern_utf16(const char *pattern, size_t plen, size_t *out_len) {
    if (plen > ((size_t)-1) / (2 * sizeof(uint16_t)) - 1) {
        return NULL;
    }
    uint16_t *out = (uint16_t *)malloc((plen + 1) * 2 * sizeof *out);
    if (out == NULL) {
        return NULL;
    }
    edit_utf8_chars_t it;
    edit_utf8_chars_init(&it, (const uint8_t *)pattern, plen, 0);
    size_t n = 0;
    uint32_t cp = 0;
    while (edit_utf8_next(&it, &cp)) {
        if (cp <= 0xFFFFU) {
            out[n++] = (uint16_t)cp;
        } else {
            uint32_t v = cp - 0x10000U;
            out[n++] = (uint16_t)(v >> 10U) | 0xD800U;
            out[n++] = (uint16_t)(v & 0x3FFU) | 0xDC00U;
        }
    }
    *out_len = n;
    return out;
}

int edit_regex_init(edit_regex_t *r, const char *pattern, size_t plen, int32_t flags,
                    edit_utext_t *text, edit_error_t *out_err) {
    if (r == NULL) {
        return -1;
    }
    r->rx = NULL;
    if (pattern == NULL || text == NULL || text->ut == NULL) {
        return -1;
    }
    if (!icu_full_load()) {
        if (out_err != NULL) {
            *out_err = edit_error_app(0);
        }
        return -1;
    }
    size_t ulen = 0;
    uint16_t *u16 = pattern_utf16(pattern, plen, &ulen);
    if (u16 == NULL || ulen > INT32_MAX) {
        free(u16);
        return -1;
    }
    int32_t status = 0;
    icu_regex_t *rx = icu_rxopen(u16, (int32_t)ulen, 8 | 512 | flags, NULL, &status);
    free(u16);
    if (rx == NULL || status > 0) {
        if (out_err != NULL) {
            *out_err = edit_error_icu((uint32_t)(status > 0 ? status : 0));
        }
        return -1;
    }
    icu_rxtime(rx, 4096, &status);
    icu_rxtext(rx, (icu_utext_s *)text->ut, &status);
    if (status > 0) {
        icu_rxclose(rx);
        if (out_err != NULL) {
            *out_err = edit_error_icu((uint32_t)status);
        }
        return -1;
    }
    r->rx = rx;
    return 0;
}

void edit_regex_destroy(edit_regex_t *r) {
    if (r == NULL || r->rx == NULL) {
        return;
    }
    icu_rxclose((icu_regex_t *)r->rx);
    r->rx = NULL;
}

void edit_regex_set_text(edit_regex_t *r, edit_utext_t *text) {
    if (r == NULL || r->rx == NULL || text == NULL || text->ut == NULL) {
        return;
    }
    int32_t status = 0;
    icu_rxtext((icu_regex_t *)r->rx, (icu_utext_s *)text->ut, &status);
    (void)status;
}

void edit_regex_reset(edit_regex_t *r, size_t index) {
    if (r == NULL || r->rx == NULL) {
        return;
    }
    if (index > INT64_MAX) {
        index = INT64_MAX;
    }
    int32_t status = 0;
    icu_rxreset((icu_regex_t *)r->rx, (int64_t)index, &status);
    (void)status;
}

bool edit_regex_next(edit_regex_t *r, size_t *out_beg, size_t *out_end) {
    if (out_beg != NULL) {
        *out_beg = 0;
    }
    if (out_end != NULL) {
        *out_end = 0;
    }
    if (r == NULL || r->rx == NULL) {
        return false;
    }
    int32_t status = 0;
    if (!icu_rxnext((icu_regex_t *)r->rx, &status) || status > 0) {
        return false;
    }
    int64_t start = icu_rxstart((icu_regex_t *)r->rx, 0, &status);
    int64_t end = icu_rxend((icu_regex_t *)r->rx, 0, &status);
    if (status > 0) {
        return false;
    }
    if (start < 0) {
        start = 0;
    }
    if (end < start) {
        end = start;
    }
    if (out_beg != NULL) {
        *out_beg = (size_t)start;
    }
    if (out_end != NULL) {
        *out_end = (size_t)end;
    }
    return true;
}
