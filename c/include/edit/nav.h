#ifndef EDIT_NAV_H
#define EDIT_NAV_H

#include <stddef.h>

#include "edit/doc.h"

#ifdef __cplusplus
extern "C" {
#endif

// VS Code-style word navigation over a readable doc (cf. Rust navigation.rs).
// Offsets clamp to the document length.
size_t edit_word_forward(const edit_doc_t *doc, size_t offset);
size_t edit_word_backward(const edit_doc_t *doc, size_t offset);
// Word range at offset, never crossing newlines. End <= len, beg <= end.
void edit_word_select(const edit_doc_t *doc, size_t offset, size_t *out_beg, size_t *out_end);

#ifdef __cplusplus
}
#endif

#endif
