#include "edit/tbuf.h"

#include <stdlib.h>
#include <string.h>

#include "edit/nav.h"
#include "tbuf_priv.h"

static uint32_t gap_generation(const void *ctx) {
    return edit_gap_generation((const edit_gap_t *)ctx);
}

// Unsafe set_cursor: no validity checks beyond internal asserts.
static void tbuf_set_cursor(edit_tbuf_t *t, edit_cursor_t cursor) {
    tbuf_set_cursor_internal(t, cursor);
    t->hist_last = 0;
    edit_point_t z = {0, 0};
    tbuf_set_selection(t, false, z, z);
}

void edit_tbuf_select_word(edit_tbuf_t *t) {
    if (t == NULL) {
        return;
    }
    edit_doc_t doc;
    edit_gap_doc(&t->buffer, &doc);
    size_t start = 0;
    size_t end = 0;
    edit_word_select(&doc, t->cursor.offset, &start, &end);
    edit_cursor_t beg = tbuf_move_to_offset(t, t->cursor, start);
    edit_cursor_t fin = tbuf_move_to_offset(t, beg, end);
    tbuf_set_cursor(t, fin);
    tbuf_set_selection(t, true, beg.logical, fin.logical);
}

void edit_tbuf_select_line(edit_tbuf_t *t) {
    if (t == NULL) {
        return;
    }
    edit_point_t b = {0, t->cursor.logical.y};
    edit_cursor_t beg = tbuf_move_to_logical(t, t->cursor, b);
    edit_point_t e = {0, t->cursor.logical.y + 1};
    edit_cursor_t fin = tbuf_move_to_logical(t, beg, e);
    tbuf_set_cursor(t, fin);
    tbuf_set_selection(t, true, beg.logical, fin.logical);
}

void edit_tbuf_select_all(edit_tbuf_t *t) {
    if (t == NULL) {
        return;
    }
    edit_cursor_t zero;
    memset(&zero, 0, sizeof zero);
    edit_point_t max = {INT32_MAX, INT32_MAX};
    edit_cursor_t fin = tbuf_move_to_logical(t, zero, max);
    tbuf_set_cursor(t, fin);
    tbuf_set_selection(t, true, zero.logical, fin.logical);
}

void edit_tbuf_start_selection(edit_tbuf_t *t) {
    if (t == NULL || t->has_selection) {
        return;
    }
    tbuf_set_selection(t, true, t->cursor.logical, t->cursor.logical);
}

void edit_tbuf_selection_update_visual(edit_tbuf_t *t, edit_point_t pos) {
    if (t == NULL) {
        return;
    }
    edit_cursor_t c = tbuf_move_to_visual(t, t->cursor, pos);
    edit_point_t beg = t->has_selection ? t->sel_beg : t->cursor.logical;
    // Note: cursor already moved by the helper below; mirror set_cursor_for_selection.
    tbuf_set_cursor_internal(t, c);
    t->hist_last = 0;
    edit_point_t end = t->cursor.logical;
    if (edit_point_eq(beg, end)) {
        edit_point_t z = {0, 0};
        tbuf_set_selection(t, false, z, z);
    } else {
        tbuf_set_selection(t, true, beg, end);
    }
}

void edit_tbuf_selection_update_logical(edit_tbuf_t *t, edit_point_t pos) {
    if (t == NULL) {
        return;
    }
    edit_cursor_t c = tbuf_move_to_logical(t, t->cursor, pos);
    edit_point_t beg = t->has_selection ? t->sel_beg : t->cursor.logical;
    tbuf_set_cursor_internal(t, c);
    t->hist_last = 0;
    edit_point_t end = t->cursor.logical;
    if (edit_point_eq(beg, end)) {
        edit_point_t z = {0, 0};
        tbuf_set_selection(t, false, z, z);
    } else {
        tbuf_set_selection(t, true, beg, end);
    }
}

void edit_tbuf_selection_update_delta(edit_tbuf_t *t, edit_move_t granularity, int32_t delta) {
    if (t == NULL) {
        return;
    }
    edit_cursor_t c = tbuf_move_delta(t, t->cursor, granularity, delta);
    edit_point_t beg = t->has_selection ? t->sel_beg : t->cursor.logical;
    tbuf_set_cursor_internal(t, c);
    t->hist_last = 0;
    edit_point_t end = t->cursor.logical;
    if (edit_point_eq(beg, end)) {
        edit_point_t z = {0, 0};
        tbuf_set_selection(t, false, z, z);
    } else {
        tbuf_set_selection(t, true, beg, end);
    }
}

bool edit_tbuf_selection_range_fb(edit_tbuf_t *t, bool line_fallback, edit_cursor_t *out_beg,
                                  edit_cursor_t *out_end) {
    if (out_beg != NULL) {
        memset(out_beg, 0, sizeof *out_beg);
    }
    if (out_end != NULL) {
        memset(out_end, 0, sizeof *out_end);
    }
    if (t == NULL) {
        return false;
    }
    edit_point_t pts[2];
    if (!t->has_selection) {
        if (!line_fallback) {
            return false;
        }
        pts[0].x = 0;
        pts[0].y = t->cursor.logical.y;
        pts[1].x = 0;
        pts[1].y = t->cursor.logical.y + 1;
    } else {
        pts[0] = t->sel_beg;
        pts[1] = t->sel_end;
        if (edit_point_cmp(pts[0], pts[1]) > 0) {
            edit_point_t tmp = pts[0];
            pts[0] = pts[1];
            pts[1] = tmp;
        }
    }
    edit_cursor_t beg = tbuf_move_to_logical(t, t->cursor, pts[0]);
    edit_cursor_t end = tbuf_move_to_logical(t, beg, pts[1]);
    if (beg.offset >= end.offset) {
        return false;
    }
    if (out_beg != NULL) {
        *out_beg = beg;
    }
    if (out_end != NULL) {
        *out_end = end;
    }
    return true;
}

bool edit_tbuf_selection_range(edit_tbuf_t *t, edit_cursor_t *out_beg, edit_cursor_t *out_end) {
    return edit_tbuf_selection_range_fb(t, false, out_beg, out_end);
}

uint8_t *edit_tbuf_extract_selection(edit_tbuf_t *t, bool del, size_t *out_len) {
    if (out_len != NULL) {
        *out_len = 0;
    }
    if (t == NULL) {
        return NULL;
    }
    edit_cursor_t beg;
    edit_cursor_t end;
    if (!edit_tbuf_selection_range_fb(t, true, &beg, &end)) {
        return NULL;
    }
    size_t n = end.offset - beg.offset;
    uint8_t *out = NULL;
    if (n > 0) {
        out = (uint8_t *)malloc(n > 0 ? n : 1);
        if (out == NULL) {
            return NULL;
        }
        size_t got = edit_gap_extract(&t->buffer, beg.offset, end.offset, out, n);
        if (got != n) {
            free(out);
            return NULL;
        }
    }
    if (del && n > 0) {
        // Undoable delete via the edit machinery.
        tbuf_edit_begin(t, 2, beg);
        tbuf_edit_delete(t, end);
        tbuf_edit_end(t);
        edit_point_t z = {0, 0};
        tbuf_set_selection(t, false, z, z);
    }
    if (out_len != NULL) {
        *out_len = n;
    }
    return out;
}

uint8_t *edit_tbuf_extract_user_selection(edit_tbuf_t *t, bool del, size_t *out_len,
                                          bool *out_has) {
    if (out_has != NULL) {
        *out_has = false;
    }
    if (out_len != NULL) {
        *out_len = 0;
    }
    if (t == NULL || !t->has_selection) {
        return NULL;
    }
    if (t->search != NULL && t->search->selection_generation == t->sel_generation) {
        return NULL;
    }
    uint8_t *out = edit_tbuf_extract_selection(t, del, out_len);
    if (out_has != NULL) {
        *out_has = true;
    }
    return out;
}

static void search_free(edit_search_t *s) {
    if (s == NULL) {
        return;
    }
    if (s->has_regex) {
        edit_regex_destroy(&s->regex);
    }
    if (s->has_text) {
        edit_utext_destroy(&s->text);
    }
    free(s->pattern);
    memset(s, 0, sizeof *s);
}

void tbuf_search_free(edit_tbuf_t *t) {
    if (t == NULL || t->search == NULL) {
        return;
    }
    search_free(t->search);
    free(t->search);
    t->search = NULL;
}

static bool opts_eq(edit_search_opts_t a, edit_search_opts_t b) {
    return a.match_case == b.match_case && a.whole_word == b.whole_word &&
           a.use_regex == b.use_regex;
}

// Builds a sanitized pattern: \b(?:p)\b for regex, \b + escaped + \b otherwise.
static char *sanitize_pattern(const char *pattern, size_t plen, bool whole_word, bool use_regex,
                              size_t *out_len) {
    static const char word_edge[] = "\\b";
    static const char group_open[] = "\\b(?:";
    static const char group_close[] = "\\b)";
    if (!whole_word) {
        char *p = (char *)malloc(plen + 1);
        if (p == NULL) {
            return NULL;
        }
        memcpy(p, pattern, plen);
        p[plen] = '\0';
        *out_len = plen;
        return p;
    }
    if (use_regex) {
        size_t n = sizeof(group_open) - 1 + plen + sizeof(group_close) - 1;
        char *p = (char *)malloc(n + 1);
        if (p == NULL) {
            return NULL;
        }
        memcpy(p, group_open, sizeof(group_open) - 1);
        memcpy(p + sizeof(group_open) - 1, pattern, plen);
        memcpy(p + sizeof(group_open) - 1 + plen, group_close, sizeof(group_close) - 1);
        p[n] = '\0';
        *out_len = n;
        return p;
    }
    size_t extra = 0;
    for (size_t i = 0; i < plen; ++i) {
        switch (pattern[i]) {
        case '*':
        case '?':
        case '+':
        case '[':
        case '(':
        case ')':
        case '{':
        case '}':
        case '^':
        case '$':
        case '|':
        case '\\':
        case '.':
            extra += 1;
            break;
        default:
            break;
        }
    }
    size_t n = (sizeof(word_edge) - 1) + plen + extra + (sizeof(word_edge) - 1);
    char *p = (char *)malloc(n + 1);
    if (p == NULL) {
        return NULL;
    }
    size_t k = 0;
    memcpy(p, word_edge, sizeof(word_edge) - 1);
    k += sizeof(word_edge) - 1;
    for (size_t i = 0; i < plen; ++i) {
        char c = pattern[i];
        switch (c) {
        case '*':
        case '?':
        case '+':
        case '[':
        case '(':
        case ')':
        case '{':
        case '}':
        case '^':
        case '$':
        case '|':
        case '\\':
        case '.':
            p[k++] = '\\';
            p[k++] = c;
            break;
        default:
            p[k++] = c;
            break;
        }
    }
    memcpy(p + k, word_edge, sizeof(word_edge) - 1);
    k += sizeof(word_edge) - 1;
    p[k] = '\0';
    *out_len = n;
    return p;
}

static edit_search_t *construct_search(edit_tbuf_t *t, const char *pattern, size_t plen,
                                       edit_search_opts_t opts) {
    size_t slen = 0;
    char *sanitized = sanitize_pattern(pattern, plen, opts.whole_word, opts.use_regex, &slen);
    if (sanitized == NULL) {
        return NULL;
    }
    int32_t flags = EDIT_REGEX_MULTILINE;
    if (!opts.match_case) {
        flags |= EDIT_REGEX_CASE_INSENSITIVE;
    }
    if (!opts.use_regex && !opts.whole_word) {
        flags |= EDIT_REGEX_LITERAL;
    }
    edit_search_t *s = (edit_search_t *)calloc(1, sizeof *s);
    if (s == NULL) {
        free(sanitized);
        return NULL;
    }
    // The doc backing the UText source must outlive this call.
    edit_gap_doc(&t->buffer, &s->doc);
    edit_usrc_t src = {&s->doc, gap_generation, &t->buffer};
    if (edit_utext_init(&s->text, &src) != 0) {
        free(sanitized);
        free(s);
        return NULL;
    }
    s->has_text = true;
    edit_error_t err;
    if (edit_regex_init(&s->regex, sanitized, slen, flags, &s->text, &err) != 0) {
        (void)err;
        edit_utext_destroy(&s->text);
        free(sanitized);
        free(s);
        return NULL;
    }
    s->has_regex = true;
    free(sanitized);
    s->pattern = (char *)malloc(plen + 1);
    if (s->pattern == NULL) {
        search_free(s);
        free(s);
        return NULL;
    }
    memcpy(s->pattern, pattern, plen);
    s->pattern[plen] = '\0';
    s->pattern_len = plen;
    s->opts = opts;
    s->buffer_generation = edit_gap_generation(&t->buffer);
    s->selection_generation = 0;
    s->next_search_offset = 0;
    s->no_matches = false;
    return s;
}

static void find_select_next(edit_tbuf_t *t, edit_search_t *s, size_t offset, bool wrap) {
    if (s->buffer_generation != edit_gap_generation(&t->buffer)) {
        edit_regex_set_text(&s->regex, &s->text);
        s->buffer_generation = edit_gap_generation(&t->buffer);
    }
    if (s->next_search_offset != offset) {
        s->next_search_offset = offset;
        edit_regex_reset(&s->regex, offset);
    }
    size_t beg = 0;
    size_t end = 0;
    bool hit = edit_regex_next(&s->regex, &beg, &end);
    if (wrap && !hit && s->next_search_offset != 0) {
        s->next_search_offset = 0;
        edit_regex_reset(&s->regex, 0);
        hit = edit_regex_next(&s->regex, &beg, &end);
    }
    if (hit) {
        s->next_search_offset = end;
        edit_cursor_t b = tbuf_move_to_offset(t, t->cursor, beg);
        edit_cursor_t e = tbuf_move_to_offset(t, b, end);
        tbuf_set_cursor(t, e);
        edit_tbuf_make_visible(t);
        s->selection_generation = tbuf_set_selection(t, true, b.logical, e.logical);
    } else {
        s->no_matches = true;
        edit_point_t z = {0, 0};
        tbuf_set_selection(t, false, z, z);
    }
}

int edit_tbuf_find_select(edit_tbuf_t *t, const char *pattern, edit_search_opts_t opts) {
    if (t == NULL || pattern == NULL) {
        return -1;
    }
    size_t plen = strlen(pattern);
    if (t->search != NULL) {
        edit_search_t *s = t->search;
        if (s->pattern_len != plen || memcmp(s->pattern, pattern, plen) != 0 ||
            !opts_eq(s->opts, opts)) {
            tbuf_search_free(t);
        }
        // When transitioning from some search to no search, move back.
        if (plen == 0 && t->has_selection) {
            edit_tbuf_goto_logical(t, t->sel_beg);
        }
    }
    if (plen == 0) {
        return 0;
    }
    if (t->search == NULL) {
        t->search = construct_search(t, pattern, plen, opts);
        if (t->search == NULL) {
            return -1;
        }
    }
    edit_search_t *s = t->search;
    if (s->no_matches) {
        return 0;
    }
    size_t offset;
    if (t->has_selection) {
        if (t->sel_generation == s->selection_generation) {
            offset = s->next_search_offset;
        } else {
            edit_point_t lo = t->sel_beg;
            if (edit_point_cmp(t->sel_end, lo) < 0) {
                lo = t->sel_end;
            }
            offset = tbuf_move_to_logical(t, t->cursor, lo).offset;
        }
    } else {
        offset = t->cursor.offset;
    }
    find_select_next(t, s, offset, true);
    return 0;
}

int edit_tbuf_find_replace(edit_tbuf_t *t, const char *pattern, edit_search_opts_t opts,
                           const char *replacement) {
    if (t == NULL || pattern == NULL || replacement == NULL) {
        return -1;
    }
    if (t->search != NULL && t->has_selection &&
        t->search->selection_generation == t->sel_generation) {
        edit_tbuf_write(t, (const uint8_t *)replacement, strlen(replacement), true);
    }
    return edit_tbuf_find_select(t, pattern, opts);
}

int edit_tbuf_find_replace_all(edit_tbuf_t *t, const char *pattern, edit_search_opts_t opts,
                               const char *replacement) {
    if (t == NULL || pattern == NULL || replacement == NULL) {
        return -1;
    }
    size_t replen = strlen(replacement);
    size_t plen = strlen(pattern);
    // A standalone search; writes clear any cached search as a side effect.
    edit_search_t *s = construct_search(t, pattern, plen, opts);
    if (s == NULL) {
        return -1;
    }
    size_t offset = 0;
    for (;;) {
        find_select_next(t, s, offset, false);
        if (!t->has_selection) {
            break;
        }
        edit_tbuf_write(t, (const uint8_t *)replacement, replen, true);
        offset = t->cursor.offset;
    }
    search_free(s);
    free(s);
    return 0;
}
