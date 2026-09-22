#ifndef EDIT_UREGEX_H
#define EDIT_UREGEX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "edit/apperr.h"
#include "edit/doc.h"

#ifdef __cplusplus
extern "C" {
#endif

// ICU regex over a readable doc via a UText provider (cf. Rust icu::Text/Regex).
// The source (doc + generation) must outlive the UText; the UText must
// outlive the regex. Single-threaded.
typedef struct {
    const edit_doc_t *doc;
    // Returns a generation counter; NULL = always treat as dirty.
    uint32_t (*generation)(const void *ctx);
    const void *gen_ctx;
} edit_usrc_t;

typedef struct {
    void *ut;        // owned UText
    edit_usrc_t src; // borrowed source (copied struct, doc must live on)
} edit_utext_t;

int edit_utext_init(edit_utext_t *t, const edit_usrc_t *src);
void edit_utext_destroy(edit_utext_t *t);

typedef struct {
    void *rx; // owned URegularExpression
} edit_regex_t;

#define EDIT_REGEX_CASE_INSENSITIVE 2
#define EDIT_REGEX_MULTILINE 8
#define EDIT_REGEX_LITERAL 16

int edit_regex_init(edit_regex_t *r, const char *pattern, size_t plen, int32_t flags,
                    edit_utext_t *text, edit_error_t *out_err);
void edit_regex_destroy(edit_regex_t *r);
void edit_regex_set_text(edit_regex_t *r, edit_utext_t *text);
void edit_regex_reset(edit_regex_t *r, size_t index);
// Next group-0 match as native (byte) range; false when done or on error.
bool edit_regex_next(edit_regex_t *r, size_t *out_beg, size_t *out_end);

#ifdef __cplusplus
}
#endif

#endif
