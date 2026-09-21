#ifndef EDIT_HELPERS_H
#define EDIT_HELPERS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Decimal units.
#define EDIT_KILO ((size_t)1000)
#define EDIT_MEGA ((size_t)1000000)
#define EDIT_GIGA ((size_t)1000000000)

// Viewport coordinate type; keep within SAFE range to avoid overflow.
typedef int32_t edit_coord_t;
#define EDIT_COORD_SAFE_MAX ((edit_coord_t)32767)
#define EDIT_COORD_SAFE_MIN ((edit_coord_t) - 32768)

typedef struct {
    edit_coord_t x;
    edit_coord_t y;
} edit_point_t;

typedef struct {
    edit_coord_t width;
    edit_coord_t height;
} edit_size_t;

typedef struct {
    edit_coord_t left;
    edit_coord_t top;
    edit_coord_t right;
    edit_coord_t bottom;
} edit_rect_t;

extern const edit_point_t EDIT_POINT_MIN;
extern const edit_point_t EDIT_POINT_MAX;

// snprintf-style: NUL-terminates, returns full length excl. NUL.
int edit_metric_format(char *dst, size_t cap, size_t value);

// Row-major order: y first, then x.
int edit_point_cmp(edit_point_t a, edit_point_t b);
bool edit_point_eq(edit_point_t a, edit_point_t b);

edit_rect_t edit_rect_one(edit_coord_t v);
edit_rect_t edit_rect_two(edit_coord_t top_bottom, edit_coord_t left_right);
edit_rect_t edit_rect_three(edit_coord_t top, edit_coord_t left_right, edit_coord_t bottom);
edit_rect_t edit_size_as_rect(edit_size_t s);
bool edit_rect_is_empty(edit_rect_t r);
// Saturate on overflow (Rust wraps; saturating is the documented C behavior).
edit_coord_t edit_rect_width(edit_rect_t r);
edit_coord_t edit_rect_height(edit_rect_t r);
bool edit_rect_contains(edit_rect_t r, edit_point_t p);
edit_rect_t edit_rect_intersect(edit_rect_t a, edit_rect_t b);

void edit_minmax_i32(int32_t a, int32_t b, int32_t out[2]);

#ifdef __cplusplus
}
#endif

#endif
