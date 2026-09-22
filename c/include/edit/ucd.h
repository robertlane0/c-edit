#ifndef EDIT_UCD_H
#define EDIT_UCD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Generated Unicode property tables (cf. Rust unicode::tables).
// Out-of-range inputs clamp to safe defaults (Rust uses unchecked indexing;
// the reachable states never go out of range, the guards are hardening).
size_t edit_ucd_lookup(uint32_t cp); // properties; cp > 0x10FFFF reads as FFFD
uint32_t edit_ucd_joins(uint32_t state, size_t lead, size_t trail);
bool edit_ucd_joins_done(uint32_t state);
size_t edit_ucd_width(size_t val); // terminal cells, uncapped
bool edit_ucd_line_joins(size_t lead, size_t trail);
size_t edit_ucd_start_props(void);
size_t edit_ucd_tab_props(void);
size_t edit_ucd_lf_props(void);

#ifdef __cplusplus
}
#endif

#endif
