#include "edit/nav.h"

typedef enum {
    NAV_WS,   // whitespace (space, tab)
    NAV_NL,   // newline (\n, \r)
    NAV_SEP,  // separator (ASCII punctuation)
    NAV_WORD, // word (everything else, incl. non-ASCII)
} nav_class_t;

static nav_class_t nav_classify(uint8_t c) {
    if (c == ' ' || c == '\t') {
        return NAV_WS;
    }
    if (c == '\n' || c == '\r') {
        return NAV_NL;
    }
    switch (c) {
    case '`':
    case '~':
    case '!':
    case '@':
    case '#':
    case '$':
    case '%':
    case '^':
    case '&':
    case '*':
    case '(':
    case ')':
    case '-':
    case '=':
    case '+':
    case '[':
    case '{':
    case ']':
    case '}':
    case '\\':
    case '|':
    case ';':
    case ':':
    case '\'':
    case '"':
    case ',':
    case '.':
    case '<':
    case '>':
    case '/':
    case '?':
        return NAV_SEP;
    default:
        return NAV_WORD;
    }
}

// Forward cursor: chunk window + absolute base offset.
typedef struct {
    const edit_doc_t *doc;
    size_t base; // absolute offset of chunk[0]
    const uint8_t *chunk;
    size_t chunk_len;
    size_t chunk_off;
} nav_fwd_t;

static void fwd_read(nav_fwd_t *n, size_t offset) {
    n->base = offset;
    n->chunk = NULL;
    n->chunk_len = 0;
    n->chunk_off = 0;
    if (n->doc != NULL && n->doc->read_fwd != NULL) {
        size_t len = 0;
        n->doc->read_fwd(n->doc->ctx, offset, &n->chunk, &len);
        n->chunk_len = (n->chunk == NULL) ? 0 : len;
    }
}

static size_t fwd_offset(const nav_fwd_t *n) {
    return n->base + n->chunk_off;
}

// Skip one newline: \n, or \r\n pair (lone \r is a control char).
static void fwd_skip_newline(nav_fwd_t *n) {
    if (n->chunk_off >= n->chunk_len) {
        return;
    }
    if (n->chunk[n->chunk_off] == '\n') {
        n->chunk_off += 1;
    } else if (n->chunk[n->chunk_off] == '\r' && n->chunk_off + 1 < n->chunk_len &&
               n->chunk[n->chunk_off + 1] == '\n') {
        n->chunk_off += 2;
    }
}

static void fwd_skip_class(nav_fwd_t *n, nav_class_t class) {
    for (;;) {
        while (n->chunk_off < n->chunk_len) {
            if (nav_classify(n->chunk[n->chunk_off]) != class) {
                return;
            }
            n->chunk_off += 1;
        }
        if (n->chunk_len == 0) {
            return;
        }
        fwd_read(n, n->base + n->chunk_len);
    }
}

size_t edit_word_forward(const edit_doc_t *doc, size_t offset) {
    if (doc == NULL) {
        return 0;
    }
    size_t len = edit_doc_len(doc);
    if (offset > len) {
        offset = len;
    }
    nav_fwd_t n;
    n.doc = doc;
    fwd_read(&n, offset);
    fwd_skip_newline(&n);
    fwd_skip_class(&n, NAV_WS);

    nav_class_t class = NAV_WS;
    if (n.chunk_off < n.chunk_len) {
        class = nav_classify(n.chunk[n.chunk_off]);
    }
    if (class == NAV_SEP || class == NAV_WORD) {
        n.chunk_off += 1;
        size_t off = fwd_offset(&n);
        fwd_skip_class(&n, class);
        if (off == fwd_offset(&n) && class == NAV_SEP) {
            fwd_skip_class(&n, NAV_WORD);
        }
    }
    return fwd_offset(&n);
}

// Backward cursor: chunk covers [base, base+len), chunk_off counts consumed.
typedef struct {
    const edit_doc_t *doc;
    size_t base;
    const uint8_t *chunk;
    size_t chunk_len;
    size_t chunk_off; // bytes remaining from the back
} nav_bwd_t;

static void bwd_read(nav_bwd_t *n, size_t offset) {
    n->base = 0;
    n->chunk = NULL;
    n->chunk_len = 0;
    n->chunk_off = 0;
    if (n->doc == NULL || n->doc->read_bwd == NULL) {
        return;
    }
    size_t len = 0;
    n->doc->read_bwd(n->doc->ctx, offset, &n->chunk, &len);
    if (n->chunk == NULL) {
        return;
    }
    n->chunk_len = len;
    n->chunk_off = len;
    // Absolute base = offset - len (offset clamped by the doc).
    n->base = offset >= len ? offset - len : 0;
}

static size_t bwd_offset(const nav_bwd_t *n) {
    return n->base + n->chunk_off;
}

// Skip one newline backwards: \n, then optional \r (lone \r also skipped).
static void bwd_skip_newline(nav_bwd_t *n) {
    if (n->chunk_off > 0 && n->chunk[n->chunk_off - 1] == '\n') {
        n->chunk_off -= 1;
    }
    if (n->chunk_off > 0 && n->chunk[n->chunk_off - 1] == '\r') {
        n->chunk_off -= 1;
    }
}

static void bwd_skip_class(nav_bwd_t *n, nav_class_t class) {
    for (;;) {
        while (n->chunk_off > 0) {
            if (nav_classify(n->chunk[n->chunk_off - 1]) != class) {
                return;
            }
            n->chunk_off -= 1;
        }
        if (n->chunk_len == 0) {
            return;
        }
        // Reload the window ending at the current absolute offset.
        size_t off = bwd_offset(n);
        if (off == 0) {
            return;
        }
        bwd_read(n, off);
        if (n->chunk_len == 0) {
            return;
        }
    }
}

size_t edit_word_backward(const edit_doc_t *doc, size_t offset) {
    if (doc == NULL) {
        return 0;
    }
    size_t len = edit_doc_len(doc);
    if (offset > len) {
        offset = len;
    }
    nav_bwd_t n;
    n.doc = doc;
    bwd_read(&n, offset);
    bwd_skip_newline(&n);
    bwd_skip_class(&n, NAV_WS);

    nav_class_t class = NAV_WS;
    if (n.chunk_off > 0) {
        class = nav_classify(n.chunk[n.chunk_off - 1]);
    }
    if (class == NAV_SEP || class == NAV_WORD) {
        n.chunk_off -= 1;
        size_t off = bwd_offset(&n);
        bwd_skip_class(&n, class);
        if (off == bwd_offset(&n) && class == NAV_SEP) {
            bwd_skip_class(&n, NAV_WORD);
        }
    }
    return bwd_offset(&n);
}

void edit_word_select(const edit_doc_t *doc, size_t offset, size_t *out_beg, size_t *out_end) {
    size_t beg = 0;
    size_t end = 0;
    if (doc != NULL) {
        size_t len = edit_doc_len(doc);
        if (offset > len) {
            offset = len;
        }
        beg = end = offset;
        nav_class_t class = NAV_NL;

        const uint8_t *chunk = NULL;
        size_t chunk_len = 0;
        if (doc->read_fwd != NULL) {
            doc->read_fwd(doc->ctx, end, &chunk, &chunk_len);
            if (chunk == NULL) {
                chunk_len = 0;
            }
        }
        if (chunk_len > 0) {
            class = nav_classify(chunk[0]);
            size_t chunk_off = 0;
            if (class != NAV_NL) {
                for (;;) {
                    chunk_off += 1;
                    end += 1;
                    if (chunk_off >= chunk_len) {
                        if (doc->read_fwd != NULL) {
                            doc->read_fwd(doc->ctx, end, &chunk, &chunk_len);
                            if (chunk == NULL) {
                                chunk_len = 0;
                            }
                        } else {
                            chunk_len = 0;
                        }
                        chunk_off = 0;
                        if (chunk_len == 0) {
                            break;
                        }
                    }
                    if (nav_classify(chunk[chunk_off]) != class) {
                        break;
                    }
                }
            }
        }

        chunk = NULL;
        chunk_len = 0;
        if (doc->read_bwd != NULL) {
            doc->read_bwd(doc->ctx, beg, &chunk, &chunk_len);
            if (chunk == NULL) {
                chunk_len = 0;
            }
        }
        if (chunk_len > 0) {
            size_t chunk_off = chunk_len;
            if (class == NAV_NL) {
                class = nav_classify(chunk[chunk_off - 1]);
            }
            if (class != NAV_NL) {
                for (;;) {
                    if (nav_classify(chunk[chunk_off - 1]) != class) {
                        break;
                    }
                    chunk_off -= 1;
                    beg -= 1;
                    if (chunk_off == 0) {
                        if (doc->read_bwd != NULL) {
                            doc->read_bwd(doc->ctx, beg, &chunk, &chunk_len);
                            if (chunk == NULL) {
                                chunk_len = 0;
                            }
                        } else {
                            chunk_len = 0;
                        }
                        chunk_off = chunk_len;
                        if (chunk_len == 0) {
                            break;
                        }
                    }
                }
            }
        }
    }
    if (out_beg != NULL) {
        *out_beg = beg;
    }
    if (out_end != NULL) {
        *out_end = end;
    }
}
