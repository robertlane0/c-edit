#define _GNU_SOURCE // Dl_info/dladdr; must precede all system headers

#include "edit/icu.h"

#include <dlfcn.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Mirrors UErrorCode values (utypes.h).
#define ICU_ZERO_ERROR 0
#define ICU_BUFFER_OVERFLOW_ERROR 15

typedef struct icu_casemap icu_casemap_t;
typedef struct icu_converter icu_converter_t;
typedef struct icu_collator icu_collator_t;
typedef icu_casemap_t *(*icu_open_fn)(const char *, uint32_t, int32_t *);
typedef int32_t (*icu_fold_fn)(const icu_casemap_t *, char *, int32_t, const char *, int32_t,
                               int32_t *);
typedef char *(*icu_available_fn)(int32_t);
typedef icu_converter_t *(*icu_cnvopen_fn)(const char *, int32_t *);
typedef void (*icu_cnvclose_fn)(icu_converter_t *);
typedef void (*icu_convert_fn)(icu_converter_t *, icu_converter_t *, char **, const char *,
                               const char **, const char *, uint16_t *, uint16_t **, uint16_t **,
                               const uint16_t *, bool, bool, int32_t *);
typedef const char *(*icu_errname_fn)(int32_t);
typedef icu_collator_t *(*icu_colopen_fn)(const char *, int32_t *);
typedef int32_t (*icu_collate_fn)(const icu_collator_t *, const char *, int32_t, const char *,
                                  int32_t, int32_t *);

#include "icu_priv.h"

static void *s_uc = NULL;
static void *s_i18n = NULL;
static char s_suffix[32] = {0};
static bool s_tried = false;
static bool s_ready = false;

static icu_casemap_t *s_casemap = NULL;
static icu_fold_fn s_fold = NULL;
static icu_available_fn s_available = NULL;
static icu_cnvopen_fn s_cnvopen = NULL;
static icu_cnvclose_fn s_cnvclose = NULL;
static icu_convert_fn s_convert = NULL;
static icu_errname_fn s_errname = NULL;
static icu_collator_t *s_collator = NULL;
static icu_colopen_fn s_colopen = NULL;
static icu_collate_fn s_collate = NULL;
icu_utextsetup_fn icu_utextsetup = NULL;
icu_utextclose_fn icu_utextclose = NULL;
icu_rxopen_fn icu_rxopen = NULL;
icu_rxclose_fn icu_rxclose = NULL;
icu_rxtime_fn icu_rxtime = NULL;
icu_rxtext_fn icu_rxtext = NULL;
icu_rxreset_fn icu_rxreset = NULL;
icu_rxnext_fn icu_rxnext = NULL;
icu_rxstart_fn icu_rxstart = NULL;
icu_rxend_fn icu_rxend = NULL;

// POSIX allows void* -> function pointer via memcpy (ISO C forbids casts).
static bool lookup(void *handle, void *slot, const char *base) {
    char name[64];
    snprintf(name, sizeof name, "%s%s", base, s_suffix);
    void *sym = dlsym(handle, name);
    if (sym == NULL) {
        return false;
    }
    memcpy(slot, &sym, sizeof sym);
    return true;
}

// ICU tags exported symbols with the major version (_78). Discover it like
// Rust sys::unix::icu_proc_suffix: unversioned u_errorName? else derive from
// the UCaseMap destructor's library path (...libicuuc.so.78.3 -> _78).
static void discover_suffix(void) {
    s_suffix[0] = '\0';
    if (dlsym(s_uc, "u_errorName") != NULL) {
        return;
    }
    void *proc = dlsym(s_uc, "_ZN8UCaseMapD1Ev");
    if (proc == NULL) {
        return;
    }
    Dl_info info;
    memset(&info, 0, sizeof info);
    if (dladdr(proc, &info) == 0 || info.dli_fname == NULL) {
        return;
    }
    char path[PATH_MAX];
    ssize_t n = readlink(info.dli_fname, path, sizeof path - 1);
    const char *p = NULL;
    size_t plen = 0;
    if (n > 0) {
        path[n] = '\0';
        p = path;
        plen = (size_t)n;
    } else {
        p = info.dli_fname;
        plen = strlen(p);
    }
    // Find ".so." then take digits up to the next '.'.
    const char *ver = NULL;
    for (size_t i = 0; i + 4 <= plen; ++i) {
        if (p[i] == '.' && p[i + 1] == 's' && p[i + 2] == 'o' && p[i + 3] == '.') {
            ver = p + i + 4;
            break;
        }
    }
    if (ver == NULL) {
        return;
    }
    size_t vlen = 0;
    while (ver[vlen] >= '0' && ver[vlen] <= '9') {
        ++vlen;
    }
    if (vlen == 0 || vlen + 2 > sizeof s_suffix) {
        return;
    }
    s_suffix[0] = '_';
    memcpy(s_suffix + 1, ver, vlen);
    s_suffix[vlen + 1] = '\0';
}

// All-or-nothing load, mirroring Rust init_if_needed.
static bool icu_load(void) {
    if (s_tried) {
        return s_ready;
    }
    s_tried = true;
    s_uc = dlopen("libicuuc.so", RTLD_LAZY);
    s_i18n = dlopen("libicui18n.so", RTLD_LAZY);
    if (s_uc == NULL || s_i18n == NULL) {
        return false;
    }
    discover_suffix();
    icu_open_fn casemap_open = NULL;
    if (!lookup(s_uc, &casemap_open, "ucasemap_open") ||
        !lookup(s_uc, &s_fold, "ucasemap_utf8FoldCase") ||
        !lookup(s_uc, &s_available, "ucnv_getAvailableName") ||
        !lookup(s_uc, &s_cnvopen, "ucnv_open") || !lookup(s_uc, &s_cnvclose, "ucnv_close") ||
        !lookup(s_uc, &s_convert, "ucnv_convertEx") || !lookup(s_uc, &s_errname, "u_errorName") ||
        !lookup(s_uc, &icu_utextsetup, "utext_setup") ||
        !lookup(s_uc, &icu_utextclose, "utext_close") || !lookup(s_i18n, &s_colopen, "ucol_open") ||
        !lookup(s_i18n, &s_collate, "ucol_strcollUTF8") ||
        !lookup(s_i18n, &icu_rxopen, "uregex_open") ||
        !lookup(s_i18n, &icu_rxclose, "uregex_close") ||
        !lookup(s_i18n, &icu_rxtime, "uregex_setTimeLimit") ||
        !lookup(s_i18n, &icu_rxtext, "uregex_setUText") ||
        !lookup(s_i18n, &icu_rxreset, "uregex_reset64") ||
        !lookup(s_i18n, &icu_rxnext, "uregex_findNext") ||
        !lookup(s_i18n, &icu_rxstart, "uregex_start64") ||
        !lookup(s_i18n, &icu_rxend, "uregex_end64")) {
        return false;
    }
    int32_t status = ICU_ZERO_ERROR;
    s_casemap = casemap_open(NULL, 0, &status);
    if (s_casemap == NULL || status > ICU_ZERO_ERROR) {
        s_casemap = NULL;
        return false;
    }
    status = ICU_ZERO_ERROR;
    s_collator = s_colopen("", &status);
    if (s_collator == NULL || status > ICU_ZERO_ERROR) {
        s_collator = NULL;
        return false;
    }
    s_ready = true;
    return true;
}

bool edit_icu_available(void) {
    return icu_load();
}

bool icu_full_load(void) {
    return icu_load();
}

static size_t fold_ascii(char *dst, size_t cap, const char *src, size_t len) {
    if (dst != NULL && cap > 0) {
        size_t k = len < cap ? len : cap - 1;
        for (size_t i = 0; i < k; ++i) {
            char c = src[i];
            dst[i] = (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
        }
        dst[k] = '\0';
    }
    return len;
}

size_t edit_fold_case(char *dst, size_t cap, const char *src, size_t len) {
    if (len > 0 && src == NULL) {
        return (size_t)(-1);
    }
    if (src == NULL) {
        src = "";
    }
    if (len > INT32_MAX || !icu_load()) {
        return fold_ascii(dst, cap, src, len);
    }
    // Guess output size, retry exact on overflow (mirrors Rust fold_case).
    char stack[256];
    char *heap = NULL;
    char *buf = stack;
    int32_t bufcap = (int32_t)sizeof stack;
    if (len + 16 > sizeof stack) {
        if (len + 16 < len) {
            return fold_ascii(dst, cap, src, len);
        }
        heap = (char *)malloc(len + 16);
        if (heap == NULL) {
            return fold_ascii(dst, cap, src, len);
        }
        buf = heap;
        bufcap = (int32_t)(len + 16);
    }
    int32_t status = ICU_ZERO_ERROR;
    int32_t outlen = s_fold(s_casemap, buf, bufcap, src, (int32_t)len, &status);
    if (status == ICU_BUFFER_OVERFLOW_ERROR && outlen > 0) {
        if ((int64_t)outlen + 1 > INT32_MAX) {
            free(heap);
            return fold_ascii(dst, cap, src, len);
        }
        char *big = (char *)malloc((size_t)outlen + 1);
        if (big == NULL) {
            free(heap);
            return fold_ascii(dst, cap, src, len);
        }
        free(heap);
        buf = heap = big;
        status = ICU_ZERO_ERROR;
        outlen = s_fold(s_casemap, buf, outlen + 1, src, (int32_t)len, &status);
    }
    if (status <= ICU_ZERO_ERROR && outlen > 0) {
        size_t need = (size_t)outlen;
        if (dst != NULL && cap > 0) {
            size_t k = need < cap ? need : cap - 1;
            memcpy(dst, buf, k);
            dst[k] = '\0';
        }
        free(heap);
        return need;
    }
    free(heap);
    return fold_ascii(dst, cap, src, len);
}

static uint8_t ascii_lower(uint8_t c) {
    return (c >= 'A' && c <= 'Z') ? (uint8_t)(c + ('a' - 'A')) : c;
}

int edit_compare_ascii(const uint8_t *a, size_t alen, const uint8_t *b, size_t blen) {
    if (a == NULL || b == NULL) {
        if (a == b) {
            return 0;
        }
        return a == NULL ? -1 : 1;
    }
    size_t n = alen < blen ? alen : blen;
    for (size_t i = 0; i < n; ++i) {
        if (a[i] != b[i]) {
            int order = a[i] < b[i] ? -1 : 1;
            uint8_t la = ascii_lower(a[i]);
            uint8_t lb = ascii_lower(b[i]);
            if (la == lb) {
                for (size_t j = i + 1; j < n; ++j) {
                    uint8_t lc = ascii_lower(a[j]);
                    uint8_t ld = ascii_lower(b[j]);
                    if (lc != ld) {
                        order = lc < ld ? -1 : 1;
                        break;
                    }
                }
            }
            return order;
        }
    }
    if (alen == blen) {
        return 0;
    }
    return alen < blen ? -1 : 1;
}

int edit_compare_strings(const uint8_t *a, size_t alen, const uint8_t *b, size_t blen) {
    if (a == NULL || b == NULL) {
        return edit_compare_ascii(a, alen, b, blen);
    }
    if (alen > INT32_MAX || blen > INT32_MAX || !icu_load()) {
        return edit_compare_ascii(a, alen, b, blen);
    }
    int32_t status = ICU_ZERO_ERROR;
    int32_t res = s_collate(s_collator, (const char *)a, (int32_t)alen, (const char *)b,
                            (int32_t)blen, &status);
    if (status > ICU_ZERO_ERROR) {
        return edit_compare_ascii(a, alen, b, blen);
    }
    if (res == 0) {
        return 0;
    }
    return res > 0 ? 1 : -1;
}

size_t edit_icu_error_text(uint32_t code, char *dst, size_t cap) {
    const char *msg = "";
    if (icu_load()) {
        const char *name = s_errname((int32_t)code);
        if (name != NULL) {
            msg = name;
        }
    }
    size_t n = strlen(msg);
    if (dst != NULL && cap > 0) {
        size_t k = n < cap ? n : cap - 1;
        memcpy(dst, msg, k);
        dst[k] = '\0';
    }
    return n;
}

size_t edit_icu_encodings(const char ***out) {
    static const char *fallback[] = {"UTF-8"};
    if (out == NULL) {
        return 0;
    }
    *out = fallback;
    if (!icu_load()) {
        return 1;
    }
    static const char **cache = NULL;
    static size_t cached = 0;
    if (cache == NULL) {
        size_t cap = 64;
        size_t n = 0;
        const char **list = (const char **)malloc(cap * sizeof *list);
        if (list == NULL) {
            return 1;
        }
        for (int32_t i = 0;; ++i) {
            char *name = s_available(i);
            if (name == NULL) {
                break;
            }
            if (n == cap) {
                if (cap > (size_t)(-1) / 2 / sizeof *list) {
                    break;
                }
                cap *= 2;
                const char **bigger = (const char **)realloc(list, cap * sizeof *list);
                if (bigger == NULL) {
                    break;
                }
                list = bigger;
            }
            list[n++] = name;
        }
        if (n == 0) {
            free(list);
            return 1;
        }
        cache = list;
        cached = n;
    }
    *out = cache;
    return cached;
}

int edit_conv_init(edit_conv_t *c, const char *src_enc, const char *dst_enc, uint16_t *pivot,
                   size_t pivot_len) {
    if (c == NULL) {
        return -1;
    }
    memset(c, 0, sizeof *c);
    if (src_enc == NULL || dst_enc == NULL || pivot == NULL || pivot_len == 0) {
        return -1;
    }
    if (!icu_load()) {
        return -1;
    }
    char sname[256];
    char dname[256];
    size_t slen = strlen(src_enc);
    size_t dlen = strlen(dst_enc);
    if (slen >= sizeof sname || dlen >= sizeof dname) {
        return -1;
    }
    memcpy(sname, src_enc, slen + 1);
    memcpy(dname, dst_enc, dlen + 1);
    int32_t status = ICU_ZERO_ERROR;
    icu_converter_t *src = s_cnvopen(sname, &status);
    icu_converter_t *dst = s_cnvopen(dname, &status);
    if (status > ICU_ZERO_ERROR || src == NULL || dst == NULL) {
        if (src != NULL) {
            s_cnvclose(src);
        }
        if (dst != NULL) {
            s_cnvclose(dst);
        }
        c->err = edit_error_icu((uint32_t)(status > 0 ? status : 0));
        c->err_set = true;
        return -1;
    }
    c->src_cnv = src;
    c->dst_cnv = dst;
    c->pivot = pivot;
    c->pivot_len = pivot_len;
    c->pivot_src = pivot;
    c->pivot_dst = pivot + pivot_len;
    c->reset = true;
    return 0;
}

void edit_conv_destroy(edit_conv_t *c) {
    if (c == NULL) {
        return;
    }
    if (c->src_cnv != NULL) {
        s_cnvclose((icu_converter_t *)c->src_cnv);
    }
    if (c->dst_cnv != NULL) {
        s_cnvclose((icu_converter_t *)c->dst_cnv);
    }
    memset(c, 0, sizeof *c);
}

int edit_conv_step(edit_conv_t *c, const uint8_t *in, size_t inlen, uint8_t *out, size_t outcap,
                   size_t *in_used, size_t *out_used) {
    if (in_used != NULL) {
        *in_used = 0;
    }
    if (out_used != NULL) {
        *out_used = 0;
    }
    if (c == NULL || c->src_cnv == NULL || c->dst_cnv == NULL) {
        return -1;
    }
    if ((inlen > 0 && in == NULL) || (outcap > 0 && out == NULL)) {
        return -1;
    }
    // ICU rejects NULL pointers even for zero-length buffers; Rust passes
    // dangling (non-null) pointers there. Use dummies the same way.
    char dummy = 0;
    const char *ip0 = inlen > 0 ? (const char *)in : &dummy;
    char *op0 = outcap > 0 ? (char *)out : &dummy;
    const char *ip = ip0;
    const char *iend = ip0 + inlen;
    char *op = op0;
    const char *oend = op0 + outcap;
    int32_t status = ICU_ZERO_ERROR;
    s_convert((icu_converter_t *)c->dst_cnv, (icu_converter_t *)c->src_cnv, &op, oend, &ip, iend,
              c->pivot, &c->pivot_src, &c->pivot_dst, c->pivot + c->pivot_len, c->reset, inlen == 0,
              &status);
    c->reset = false;
    if (status > ICU_ZERO_ERROR && status != ICU_BUFFER_OVERFLOW_ERROR) {
        c->err = edit_error_icu((uint32_t)status);
        c->err_set = true;
        return -1;
    }
    if (in_used != NULL) {
        *in_used = (size_t)(ip - ip0);
    }
    if (out_used != NULL) {
        *out_used = (size_t)(op - op0);
    }
    return 0;
}

bool edit_conv_error(const edit_conv_t *c, edit_error_t *out_err) {
    if (c == NULL || !c->err_set) {
        return false;
    }
    if (out_err != NULL) {
        *out_err = c->err;
    }
    return true;
}
