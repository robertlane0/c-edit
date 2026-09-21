#include "edit/oklab.h"

#include <math.h>
#include <string.h>

#include "oklab_lut.inc"

static float clamp01(float v) {
    if (v < 0.0f) {
        return 0.0f;
    }
    if (v > 1.0f) {
        return 1.0f;
    }
    return v;
}

// Fast cbrt for [0,1]; max err < 6.7e-4, enough for UI blending.
static float cbrtf_est(float a) {
    uint32_t u = 0;
    memcpy(&u, &a, sizeof u);
    u = u / 3U + 709921077U;
    float x = 0.0f;
    memcpy(&x, &u, sizeof x);
    return (1.0f / 3.0f) * (a / (x * x) + (x + x));
}

static uint32_t linear_to_srgb(float c) {
    float v = 0.0f;
    if (c > 0.0031308f) {
        v = 255.0f * 1.055f * powf(c, 1.0f / 2.4f) - 255.0f * 0.055f;
    } else {
        v = 255.0f * 12.92f * c;
    }
    return (uint32_t)v;
}

edit_lab_t edit_srgb_to_oklab(uint32_t color) {
    float r = kSrgbToRgb[color & 0xFFU];
    float g = kSrgbToRgb[(color >> 8U) & 0xFFU];
    float b = kSrgbToRgb[(color >> 16U) & 0xFFU];
    float alpha = (float)(color >> 24U) * (1.0f / 255.0f);

    float l = 0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b;
    float m = 0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b;
    float s = 0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b;

    float l_ = cbrtf_est(l);
    float m_ = cbrtf_est(m);
    float s_ = cbrtf_est(s);

    edit_lab_t out = {
        0.2104542553f * l_ + 0.7936177850f * m_ - 0.0040720468f * s_,
        1.9779984951f * l_ - 2.4285922050f * m_ + 0.4505937099f * s_,
        0.0259040371f * l_ + 0.7827717662f * m_ - 0.8086757660f * s_,
        alpha,
    };
    return out;
}

uint32_t edit_oklab_to_srgb(edit_lab_t c) {
    float l_ = c.l + 0.3963377774f * c.a + 0.2158037573f * c.b;
    float m_ = c.l - 0.1055613458f * c.a - 0.0638541728f * c.b;
    float s_ = c.l - 0.0894841775f * c.a - 1.2914855480f * c.b;

    float l = l_ * l_ * l_;
    float m = m_ * m_ * m_;
    float s = s_ * s_ * s_;

    float r = 4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s;
    float g = -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s;
    float b = -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s;

    uint32_t ri = linear_to_srgb(clamp01(r));
    uint32_t gi = linear_to_srgb(clamp01(g));
    uint32_t bi = linear_to_srgb(clamp01(b));
    uint32_t ai = (uint32_t)(clamp01(c.alpha) * 255.0f);

    return ri | (gi << 8U) | (bi << 16U) | (ai << 24U);
}

uint32_t edit_oklab_blend(uint32_t dst, uint32_t src) {
    edit_lab_t d = edit_srgb_to_oklab(dst);
    edit_lab_t s = edit_srgb_to_oklab(src);

    float inv_a = 1.0f - s.alpha;
    edit_lab_t out = {
        s.l + d.l * inv_a,
        s.a + d.a * inv_a,
        s.b + d.b * inv_a,
        s.alpha + d.alpha * inv_a,
    };
    return edit_oklab_to_srgb(out);
}
