#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "edit/oklab.h"

static int checks = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #cond);                                     \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static int close(float a, float b, float eps) {
    ++checks;
    if (fabsf(a - b) > eps) {
        fprintf(stderr, "FAIL %d: %f vs %f\n", __LINE__, (double)a, (double)b);
        return 1;
    }
    return 0;
}

// Per-channel distance; libm powf may differ by 1 ulp across builds.
static int close_color(uint32_t a, uint32_t b, uint32_t tol) {
    for (int i = 0; i < 4; ++i) {
        uint32_t ca = (a >> (8 * i)) & 0xFFU;
        uint32_t cb = (b >> (8 * i)) & 0xFFU;
        uint32_t d = ca > cb ? ca - cb : cb - ca;
        ++checks;
        if (d > tol) {
            fprintf(stderr, "FAIL %d: %#x vs %#x\n", __LINE__, a, b);
            return 1;
        }
    }
    return 0;
}

int main(void) {
    // Reference vectors from Rust edit::oklab.
    struct {
        uint32_t srgb;
        float l;
        float a;
        float b;
        float alpha;
        uint32_t roundtrip;
    } cases[] = {
        {0xFF000000, 0.0f, -0.0f, 0.0f, 1.0f, 0xFF000000},
        {0xFFFFFFFF, 1.000360250f, -0.000000030f, 0.0f, 1.0f, 0xFFFFFFFF},
        {0xFFFF0000, 0.452147156f, -0.032624811f, -0.311413676f, 1.0f, 0xFFFE0000},
        {0xFF00FF00, 0.866511047f, -0.233348906f, 0.179306269f, 1.0f, 0xFF01FE06},
        {0xFF0000FF, 0.627995253f, 0.225237325f, 0.125828415f, 1.0f, 0xFF0000FF},
        {0xFF808080, 0.599881530f, 0.000000060f, 0.000000030f, 1.0f, 0xFF808080},
        {0xFF123456, 0.359616518f, 0.031803191f, 0.060170189f, 1.0f, 0xFF123356},
        {0x80123456, 0.359616518f, 0.031803191f, 0.060170189f, 0.501960814f, 0x80123356},
        {0x00000000, 0.0f, -0.0f, 0.0f, 0.0f, 0x00000000},
        {0xFFABCDEF, 0.868707776f, 0.023566663f, 0.054126203f, 1.0f, 0xFFABCCEF},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        edit_lab_t lab = edit_srgb_to_oklab(cases[i].srgb);
        if (close(lab.l, cases[i].l, 1e-6f) != 0) {
            return 1;
        }
        if (close(lab.a, cases[i].a, 1e-6f) != 0) {
            return 1;
        }
        if (close(lab.b, cases[i].b, 1e-6f) != 0) {
            return 1;
        }
        if (close(lab.alpha, cases[i].alpha, 1e-6f) != 0) {
            return 1;
        }
        if (close_color(edit_oklab_to_srgb(lab), cases[i].roundtrip, 1) != 0) {
            return 1;
        }
    }

    // Blend references from Rust.
    CHECK(close_color(edit_oklab_blend(0xFF000000, 0xFFFFFFFF), 0xFFFFFFFF, 1) == 0);
    CHECK(close_color(edit_oklab_blend(0xFF123456, 0x80765432), 0xFF9A8577, 1) == 0);
    CHECK(close_color(edit_oklab_blend(0xFFFF0000, 0x8000FF00), 0xFFFFFF00, 1) == 0);

    // Identities: opaque src wins; transparent src still adds its RGB (Rust quirk).
    CHECK(edit_oklab_blend(0xFF112233, 0xFF445566) ==
          edit_oklab_to_srgb(edit_srgb_to_oklab(0xFF445566)));
    CHECK(close_color(edit_oklab_blend(0xFF112233, 0x00123456), 0xFF3E79B5, 1) == 0);

    printf("test_oklab: %d checks passed\n", checks);
    return 0;
}
