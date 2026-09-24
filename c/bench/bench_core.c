// Core benchmarks for the C port (cf. benches/lib.rs).
//
// Mirrors the Rust workloads: hash, oklab, simd memchr2/memset, unicode
// measurement + UTF-8 iteration. Each benchmark runs its own tight loop (like
// criterion's b.iter) so the compiler can inline the call, matching the Rust
// measurement shape. Prints "name size ns/op bytes/s" lines.
//
// tools/bench_compare.py matches these against criterion output.

#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "edit/doc.h"
#include "edit/hash.h"
#include "edit/helpers.h"
#include "edit/measure.h"
#include "edit/oklab.h"
#include "edit/simd.h"
#include "edit/utf8.h"

static volatile uint64_t sink;

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void report(const char *name, size_t size, double best, int iters) {
    double per_op = best * 1e9 / (double)iters;
    double bps = (double)size * (double)iters / best;
    printf("%s %zu %.3f %.3f\n", name, size, per_op, bps);
}

// Best-of-5 wall time over `iters` calls, with 1000 warmup calls.
// The barrier keeps the optimizer from hoisting loop-invariant work out of
// the timed loop (constant inputs would otherwise be precomputed).
#if defined(__GNUC__)
#define BARRIER() __asm__ __volatile__("" ::: "memory")
#else
#define BARRIER() ((void)0)
#endif

#define TIME(body, iters)                                                                          \
    do {                                                                                           \
        for (int i = 0; i < 1000; ++i) {                                                           \
            body;                                                                                  \
        }                                                                                          \
        double best = 1e30;                                                                        \
        for (int rep = 0; rep < 5; ++rep) {                                                        \
            double beg = now_sec();                                                                \
            for (int i = 0; i < (iters); ++i) {                                                    \
                BARRIER();                                                                         \
                body;                                                                              \
            }                                                                                      \
            double el = now_sec() - beg;                                                           \
            if (el > 0.0 && el < best) {                                                           \
                best = el;                                                                         \
            }                                                                                      \
        }                                                                                          \
        TIME_RESULT = best;                                                                        \
    } while (0)

#define RUN(name, size, iters, body)                                                               \
    do {                                                                                           \
        double TIME_RESULT = 0.0;                                                                  \
        TIME(body, iters);                                                                         \
        report(name, size, TIME_RESULT, iters);                                                    \
    } while (0)

int main(void) {
    uint8_t data8[8] = {0};
    uint8_t data16[16] = {0};
    static uint8_t data1024[1024];

    // Varying seeds/inputs keep the optimizer from precomputing constant work.
    RUN("hash/8", 8, 1000000, sink = edit_hash((uint64_t)i, data8, sizeof data8));
    RUN("hash/16", 16, 1000000, sink = edit_hash((uint64_t)i, data16, sizeof data16));
    RUN("hash/1024", 1024, 200000, sink = edit_hash((uint64_t)i, data1024, sizeof data1024));

    RUN("oklab/srgb_to_oklab", 4, 1000000, {
        edit_lab_t c = edit_srgb_to_oklab(0xFF212CBE ^ (uint32_t)i);
        sink += (uint64_t)(uint32_t)(c.l * 100.0f);
    });
    RUN("oklab/oklab_blend", 4, 500000, sink += edit_oklab_blend(0x7F212CBE, (uint32_t)i));

    const size_t sizes[4] = {8, 40, 72, 1024 + 8};
    uint8_t *buffer = (uint8_t *)malloc(2048);
    if (buffer == NULL) {
        return 1;
    }
    for (int i = 0; i < 4; ++i) {
        size_t n = sizes[i];
        memset(buffer, 'a', n);
        buffer[n] = '\n';
        char label[64];
        snprintf(label, sizeof label, "simd/memchr2/%zu", n);
        // Rust scans the whole 2048-byte buffer with the match at `size`.
        RUN(label, n + 1, 1000000,
            sink += (uint64_t)edit_memchr2((uint8_t)'\n', (uint8_t)'\r', buffer, 2048, 0));
    }

    uint32_t *buf32 = (uint32_t *)calloc(2048, 1);
    uint8_t *buf8 = (uint8_t *)calloc(2048, 1);
    if (buf32 == NULL || buf8 == NULL) {
        free(buffer);
        free(buf32);
        free(buf8);
        return 1;
    }
    for (int i = 0; i < 4; ++i) {
        size_t n = sizes[i];
        char label[64];
        snprintf(label, sizeof label, "simd/memset<u32>/%zu", n);
        RUN(label, n, 1000000, {
            edit_memset_u32(buf32, (uint32_t)i, n / sizeof(uint32_t));
            sink += buf32[0];
        });
    }
    for (int i = 0; i < 4; ++i) {
        size_t n = sizes[i];
        char label[64];
        snprintf(label, sizeof label, "simd/memset<u8>/%zu", n);
        RUN(label, n, 1000000, {
            edit_memset_u8(buf8, (uint8_t)i, n);
            sink += buf8[0];
        });
    }

    static const char *const TEXT =
        "In the quiet twilight, dreams unfold, soft whispers of a story untold.\n"
        "月明かりが静かに照らし出し、夢を見る心の奥で詩が静かに囁かれる\n"
        "Stars collide in the early light of hope, echoing the silent call of the night.\n"
        "夜の静寂、希望と孤独が混ざり合うその中で詩が永遠に続く\n";
    size_t line = strlen(TEXT);
    size_t text_len = line * 10;
    uint8_t *text = (uint8_t *)malloc(text_len);
    if (text == NULL) {
        free(buffer);
        free(buf32);
        free(buf8);
        return 1;
    }
    for (size_t i = 0; i < 10; ++i) {
        memcpy(text + i * line, TEXT, line);
    }
    edit_slice_doc_t slice;
    edit_slice_doc_init(&slice, text, text_len);

    RUN("unicode/MeasurementConfig/goto_logical/basic", text_len, 20000, {
        edit_measure_t m;
        edit_measure_init(&m, &slice.doc);
        sink +=
            (uint64_t)edit_measure_goto_logical(&m, (edit_point_t){INT32_MAX, INT32_MAX}).offset;
    });
    RUN("unicode/MeasurementConfig/goto_logical/word_wrap", text_len, 20000, {
        edit_measure_t m;
        edit_measure_init(&m, &slice.doc);
        edit_measure_set_wrap(&m, 50);
        sink +=
            (uint64_t)edit_measure_goto_logical(&m, (edit_point_t){INT32_MAX, INT32_MAX}).offset;
    });
    RUN("unicode/Utf8Chars/next", text_len, 20000, {
        edit_utf8_chars_t it;
        edit_utf8_chars_init(&it, slice.bytes, slice.len, 0);
        uint32_t acc = 0;
        while (edit_utf8_next(&it, &acc)) {
            sink += acc;
        }
    });

    free(text);
    free(buffer);
    free(buf32);
    free(buf8);
    return 0;
}
