#include "edit/helpers.h"

#include <limits.h>
#include <stdio.h>

const edit_point_t EDIT_POINT_MIN = {INT32_MIN, INT32_MIN};
const edit_point_t EDIT_POINT_MAX = {INT32_MAX, INT32_MAX};

int edit_metric_format(char *dst, size_t cap, size_t value) {
    size_t v = value;
    const char *suffix = "B";
    if (v >= EDIT_GIGA) {
        v /= EDIT_GIGA;
        suffix = "GB";
    } else if (v >= EDIT_MEGA) {
        v /= EDIT_MEGA;
        suffix = "MB";
    } else if (v >= EDIT_KILO) {
        v /= EDIT_KILO;
        suffix = "kB";
    }
    if (dst == NULL || cap == 0) {
        // Length query; cap the estimate below INT_MAX.
        size_t n = 0;
        size_t t = v;
        do {
            ++n;
            t /= 10;
        } while (t > 0);
        n += (suffix[1] == '\0') ? 1 : 2;
        return n > (size_t)INT_MAX ? INT_MAX : (int)n;
    }
    return snprintf(dst, cap, "%zu%s", v, suffix);
}

int edit_point_cmp(edit_point_t a, edit_point_t b) {
    if (a.y != b.y) {
        return a.y < b.y ? -1 : 1;
    }
    if (a.x != b.x) {
        return a.x < b.x ? -1 : 1;
    }
    return 0;
}

bool edit_point_eq(edit_point_t a, edit_point_t b) {
    return a.x == b.x && a.y == b.y;
}

edit_rect_t edit_rect_one(edit_coord_t v) {
    edit_rect_t r = {v, v, v, v};
    return r;
}

edit_rect_t edit_rect_two(edit_coord_t top_bottom, edit_coord_t left_right) {
    edit_rect_t r = {left_right, top_bottom, left_right, top_bottom};
    return r;
}

edit_rect_t edit_rect_three(edit_coord_t top, edit_coord_t left_right, edit_coord_t bottom) {
    edit_rect_t r = {left_right, top, left_right, bottom};
    return r;
}

edit_rect_t edit_size_as_rect(edit_size_t s) {
    edit_rect_t r = {0, 0, s.width, s.height};
    return r;
}

bool edit_rect_is_empty(edit_rect_t r) {
    return r.left >= r.right || r.top >= r.bottom;
}

static edit_coord_t saturate_i64(int64_t v) {
    if (v > INT32_MAX) {
        return INT32_MAX;
    }
    if (v < INT32_MIN) {
        return INT32_MIN;
    }
    return (edit_coord_t)v;
}

edit_coord_t edit_rect_width(edit_rect_t r) {
    return saturate_i64((int64_t)r.right - (int64_t)r.left);
}

edit_coord_t edit_rect_height(edit_rect_t r) {
    return saturate_i64((int64_t)r.bottom - (int64_t)r.top);
}

bool edit_rect_contains(edit_rect_t r, edit_point_t p) {
    return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
}

edit_rect_t edit_rect_intersect(edit_rect_t a, edit_rect_t b) {
    edit_coord_t l = a.left > b.left ? a.left : b.left;
    edit_coord_t t = a.top > b.top ? a.top : b.top;
    edit_coord_t r = a.right < b.right ? a.right : b.right;
    edit_coord_t bot = a.bottom < b.bottom ? a.bottom : b.bottom;
    // Clamp to non-negative size.
    if (r < l) {
        r = l;
    }
    if (bot < t) {
        bot = t;
    }
    edit_rect_t out = {l, t, r, bot};
    return out;
}

void edit_minmax_i32(int32_t a, int32_t b, int32_t out[2]) {
    if (out == NULL) {
        return;
    }
    if (b < a) {
        out[0] = b;
        out[1] = a;
    } else {
        out[0] = a;
        out[1] = b;
    }
}
