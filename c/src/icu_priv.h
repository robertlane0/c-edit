// Shared ICU loader slots for this TU set only (not public API).
#ifndef EDIT_ICU_PRIV_H
#define EDIT_ICU_PRIV_H

#include <stdbool.h>
#include <stdint.h>

typedef struct icu_casemap icu_casemap_t;
typedef struct icu_regex icu_regex_t;

// Exact ICU UText layout (utext.h); field order verified against headers.
typedef struct {
    uint32_t magic;
    int32_t flags;
    int32_t provider_properties;
    int32_t size_of_struct;
    int64_t chunk_native_limit;
    int32_t extra_size;
    int32_t native_indexing_limit;
    int64_t chunk_native_start;
    int32_t chunk_offset;
    int32_t chunk_length;
    const uint16_t *chunk_contents;
    const void *p_funcs;
    void *p_extra;
    const void *context;
    const void *p;
    const void *q;
    const void *r;
    void *priv_p;
    int64_t a;
    int32_t b;
    int32_t c;
    int64_t priv_a;
    int32_t priv_b;
    int32_t priv_c;
} icu_utext_s;

typedef icu_utext_s *(*icu_clone_fn)(icu_utext_s *, const icu_utext_s *, bool, int32_t *);
typedef int64_t (*icu_nlen_fn)(icu_utext_s *);
typedef bool (*icu_access_fn)(icu_utext_s *, int64_t, bool);
typedef int64_t (*icu_mapoff_fn)(const icu_utext_s *);
typedef int32_t (*icu_mapidx_fn)(const icu_utext_s *, int64_t);

typedef struct {
    int32_t table_size;
    int32_t reserved1;
    int32_t reserved2;
    int32_t reserved3;
    icu_clone_fn clone_fn;
    icu_nlen_fn native_length_fn;
    icu_access_fn access_fn;
    void *extract_fn;
    void *replace_fn;
    void *copy_fn;
    icu_mapoff_fn map_offset_fn;
    icu_mapidx_fn map_index_fn;
    void *close_fn;
    void *spare1;
    void *spare2;
    void *spare3;
} icu_ufuncs_s;
typedef icu_utext_s *(*icu_utextsetup_fn)(icu_utext_s *, int32_t, int32_t *);
typedef icu_utext_s *(*icu_utextclose_fn)(icu_utext_s *);
typedef icu_regex_t *(*icu_rxopen_fn)(const uint16_t *, int32_t, int32_t, void *, int32_t *);
typedef void (*icu_rxclose_fn)(icu_regex_t *);
typedef void (*icu_rxtime_fn)(icu_regex_t *, int32_t, int32_t *);
typedef void (*icu_rxtext_fn)(icu_regex_t *, icu_utext_s *, int32_t *);
typedef void (*icu_rxreset_fn)(icu_regex_t *, int64_t, int32_t *);
typedef bool (*icu_rxnext_fn)(icu_regex_t *, int32_t *);
typedef int64_t (*icu_rxstart_fn)(icu_regex_t *, int32_t, int32_t *);
typedef int64_t (*icu_rxend_fn)(icu_regex_t *, int32_t, int32_t *);

extern icu_utextsetup_fn icu_utextsetup;
extern icu_utextclose_fn icu_utextclose;
extern icu_rxopen_fn icu_rxopen;
extern icu_rxclose_fn icu_rxclose;
extern icu_rxtime_fn icu_rxtime;
extern icu_rxtext_fn icu_rxtext;
extern icu_rxreset_fn icu_rxreset;
extern icu_rxnext_fn icu_rxnext;
extern icu_rxstart_fn icu_rxstart;
extern icu_rxend_fn icu_rxend;

// Full all-or-nothing load gate (same as edit_icu_available).
bool icu_full_load(void);

#endif
