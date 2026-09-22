#include "edit/ucd.h"

#include "ucd_tables.inc"

size_t edit_ucd_lookup(uint32_t cp) {
    if (cp > 0x10FFFFU) {
        cp = 0xFFFDU;
    }
    if (cp < 0x80U) {
        return kUcdStage3[cp];
    }
    size_t s = kUcdStage0[cp >> 11U];
    size_t i = s + ((cp >> 5U) & 63U);
    if (i >= sizeof kUcdStage1 / sizeof kUcdStage1[0]) {
        return 0;
    }
    s = kUcdStage1[i];
    i = s + ((cp >> 2U) & 7U);
    if (i >= sizeof kUcdStage2 / sizeof kUcdStage2[0]) {
        return 0;
    }
    s = kUcdStage2[i];
    i = s + (cp & 3U);
    if (i >= sizeof kUcdStage3 / sizeof kUcdStage3[0]) {
        return 0;
    }
    return kUcdStage3[i];
}

uint32_t edit_ucd_joins(uint32_t state, size_t lead, size_t trail) {
    // Reachable property classes are < 16 (generator-packed); anything else
    // stops the cluster, as does a state outside the 2 table rows.
    size_t l = lead & 31U;
    size_t t = trail & 31U;
    if (state >= 2 || l >= 16 || t >= 16) {
        return 3;
    }
    return (kUcdGraphemeJoin[state][l] >> (t * 2U)) & 3U;
}

bool edit_ucd_joins_done(uint32_t state) {
    return state == 3;
}

size_t edit_ucd_width(size_t val) {
    return val >> 11U;
}

bool edit_ucd_line_joins(size_t lead, size_t trail) {
    size_t l = (lead >> 6U) & 31U;
    size_t t = (trail >> 6U) & 31U;
    if (l >= sizeof kUcdLineBreakJoin / sizeof kUcdLineBreakJoin[0]) {
        return false;
    }
    return ((kUcdLineBreakJoin[l] >> t) & 1U) != 0;
}

size_t edit_ucd_start_props(void) {
    return 0x603;
}

size_t edit_ucd_tab_props(void) {
    return 0x963;
}

size_t edit_ucd_lf_props(void) {
    return 0x802;
}
