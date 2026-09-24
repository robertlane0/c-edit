#include "edit/tbuf.h"

#include <stdlib.h>
#include <string.h>

// Reference-counted TextBuffer for shared editor widgets (cf. RcTextBuffer).
// Single-threaded; last release destroys the buffer.
int edit_shared_tbuf_create(edit_shared_tbuf_t **out, bool small) {
    if (out == NULL) {
        return -1;
    }
    *out = NULL;
    edit_shared_tbuf_t *s = (edit_shared_tbuf_t *)malloc(sizeof *s);
    if (s == NULL) {
        return -1;
    }
    memset(s, 0, sizeof *s);
    if (edit_tbuf_init(&s->tbuf, small) != 0) {
        free(s);
        return -1;
    }
    s->refs = 1;
    *out = s;
    return 0;
}

void edit_shared_retain(edit_shared_tbuf_t *s) {
    if (s != NULL) {
        s->refs += 1;
    }
}

void edit_shared_release(edit_shared_tbuf_t *s) {
    if (s == NULL) {
        return;
    }
    s->refs -= 1;
    if (s->refs <= 0) {
        edit_tbuf_destroy(&s->tbuf);
        free(s);
    }
}
