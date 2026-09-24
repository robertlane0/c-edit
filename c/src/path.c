#include "edit/path.h"

#include <stdlib.h>
#include <string.h>

#define EDIT_PATH_ERR ((size_t)(-1))

size_t edit_path_normalize(char *dst, size_t cap, const char *path) {
    if (path == NULL || path[0] != '/') {
        return EDIT_PATH_ERR;
    }

    // In-place compaction on a heap copy; backward scan rewinds past ..
    size_t n = strlen(path);
    if (n >= EDIT_PATH_ERR - 1) {
        return EDIT_PATH_ERR;
    }
    char *buf = (char *)malloc(n + 1);
    if (buf == NULL) {
        return EDIT_PATH_ERR;
    }
    memcpy(buf, path, n + 1);

    size_t w = 1; // buf[0] stays '/'
    size_t r = 1;
    while (r < n) {
        size_t s = r;
        while (r < n && buf[r] != '/') {
            ++r;
        }
        size_t e = r; // [s, e) is the component
        if (r < n) {
            ++r; // skip separator
        }
        if (e == s) {
            continue; // empty from //
        }
        size_t clen = e - s;
        if (clen == 1 && buf[s] == '.') {
            continue;
        }
        if (clen == 2 && buf[s] == '.' && buf[s + 1] == '.') {
            if (w > 1) {
                do {
                    --w;
                } while (buf[w] != '/');
                if (w == 0) {
                    w = 1;
                }
            }
            continue;
        }
        if (w > 1) {
            buf[w++] = '/';
        }
        // Reads lag writes (s >= w); memmove for safety.
        memmove(buf + w, buf + s, clen);
        w += clen;
    }
    buf[w] = '\0';

    size_t need = w;
    if (dst != NULL && cap > 0) {
        size_t k = need < cap ? need : cap - 1;
        memcpy(dst, buf, k);
        dst[k] = '\0';
    }
    free(buf);
    return need;
}
