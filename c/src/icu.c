#define _GNU_SOURCE // Dl_info/dladdr; must precede all system headers

#include "edit/icu.h"

#include <dlfcn.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Mirrors UErrorCode values (utypes.h); only these two are used here.
#define ICU_ZERO_ERROR 0
#define ICU_BUFFER_OVERFLOW_ERROR 15

typedef struct icu_casemap icu_casemap_t;
typedef icu_casemap_t *(*icu_open_fn)(const char *, uint32_t, int32_t *);
typedef int32_t (*icu_fold_fn)(const icu_casemap_t *, char *, int32_t, const char *, int32_t,
                               int32_t *);

static void *s_handle = NULL;
static icu_casemap_t *s_casemap = NULL;
static icu_fold_fn s_fold = NULL;
static bool s_tried = false;

// POSIX allows void* -> function pointer via memcpy (ISO C forbids casts).
static bool lookup(void *slot, const char *name) {
    void *sym = dlsym(s_handle, name);
    if (sym == NULL) {
        return false;
    }
    memcpy(slot, &sym, sizeof sym);
    return true;
}

// ICU tags exported symbols with the major version (_78). Discover it like
// Rust sys::unix::icu_proc_suffix: unversioned u_errorName? else derive from
// the UCaseMap destructor's library path (...libicuuc.so.78.3 -> _78).
static void discover_suffix(char *out, size_t cap) {
    out[0] = '\0';
    if (dlsym(s_handle, "u_errorName") != NULL) {
        return;
    }
    void *proc = dlsym(s_handle, "_ZN8UCaseMapD1Ev");
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
    if (vlen == 0 || vlen + 2 > cap) {
        return;
    }
    out[0] = '_';
    memcpy(out + 1, ver, vlen);
    out[vlen + 1] = '\0';
}

static bool icu_load(void) {
    if (s_tried) {
        return s_casemap != NULL;
    }
    s_tried = true;
    s_handle = dlopen("libicuuc.so", RTLD_LAZY);
    if (s_handle == NULL) {
        return false;
    }
    char suffix[32];
    discover_suffix(suffix, sizeof suffix);
    char open_name[64];
    char fold_name[64];
    snprintf(open_name, sizeof open_name, "ucasemap_open%s", suffix);
    snprintf(fold_name, sizeof fold_name, "ucasemap_utf8FoldCase%s", suffix);
    icu_open_fn open_fn = NULL;
    if (!lookup(&open_fn, open_name) || !lookup(&s_fold, fold_name)) {
        s_fold = NULL;
        return false;
    }
    int32_t status = ICU_ZERO_ERROR;
    s_casemap = open_fn(NULL, 0, &status);
    if (s_casemap == NULL || status > ICU_ZERO_ERROR) {
        s_casemap = NULL;
        s_fold = NULL;
        return false;
    }
    return true;
}

bool edit_icu_available(void) {
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
        return (size_t)-1;
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
