#ifndef EDIT_OKLAB_H
#define EDIT_OKLAB_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Oklab color with alpha; layout matches Rust Lab{l, a, b, alpha}.
typedef struct {
    float l;
    float a;
    float b;
    float alpha;
} edit_lab_t;

// sRGB 0xAABBGGRR to Oklab.
edit_lab_t edit_srgb_to_oklab(uint32_t color);
// Oklab to sRGB 0xAABBGGRR; clamps out-of-gamut channels.
uint32_t edit_oklab_to_srgb(edit_lab_t c);
// Alpha-composite src over dst in Oklab space.
uint32_t edit_oklab_blend(uint32_t dst, uint32_t src);

#ifdef __cplusplus
}
#endif

#endif
