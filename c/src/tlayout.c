#include "edit/tree.h"

#include <limits.h>
#include <stddef.h>

void edit_tree_make_table(edit_tnode_t *node) {
    if (node != NULL) {
        node->content_kind = 1;
    }
}

void edit_tree_make_scrollarea(edit_tnode_t *node) {
    if (node != NULL) {
        node->content_kind = 2;
    }
}

static int32_t clamp32(int32_t v, int32_t lo, int32_t hi) {
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static int32_t sat_f32(float v) {
    if (!(v > (float)INT32_MIN)) {
        return INT32_MIN;
    }
    if (!(v < (float)INT32_MAX)) {
        return INT32_MAX;
    }
    return (int32_t)v;
}

static void outer_to_inner(const edit_tnode_t *n, edit_rect_t *outer) {
    int32_t l = n->attributes.bordered ? 1 : 0;
    int32_t t = n->attributes.bordered ? 1 : 0;
    int32_t r = (n->attributes.bordered || n->content_kind == 2) ? 1 : 0;
    int32_t b = n->attributes.bordered ? 1 : 0;
    outer->left += n->attributes.padding.left + l;
    outer->top += n->attributes.padding.top + t;
    outer->right -= n->attributes.padding.right + r;
    outer->bottom -= n->attributes.padding.bottom + b;
}

static edit_size_t intrinsic_to_outer(const edit_tnode_t *n) {
    int32_t l = n->attributes.bordered ? 1 : 0;
    int32_t t = n->attributes.bordered ? 1 : 0;
    int32_t r = (n->attributes.bordered || n->content_kind == 2) ? 1 : 0;
    int32_t b = n->attributes.bordered ? 1 : 0;
    edit_size_t s = n->intrinsic_size;
    s.width += n->attributes.padding.left + n->attributes.padding.right + l + r;
    s.height += n->attributes.padding.top + n->attributes.padding.bottom + t + b;
    return s;
}

// Appends a zero column; false on OOM.
static bool columns_grow(edit_tree_t *tree, edit_tnode_t *node) {
    if (node->table_ncols < node->table_capcols) {
        return true;
    }
    size_t grown = node->table_capcols != 0 ? node->table_capcols * 2 : 4;
    void *mem = NULL;
    if (node->table_columns == NULL) {
        if (!edit_arena_alloc(tree->arena, grown * sizeof(int32_t), _Alignof(int32_t), &mem)) {
            return false;
        }
    } else {
        if (!edit_arena_grow(tree->arena, node->table_columns,
                             node->table_capcols * sizeof(int32_t), grown * sizeof(int32_t),
                             _Alignof(int32_t), &mem)) {
            return false;
        }
    }
    node->table_columns = (int32_t *)mem;
    node->table_capcols = grown;
    return true;
}

static void compute_intrinsic(edit_tree_t *tree, edit_tnode_t *node) {
    if (node->content_kind == 1) {
        for (edit_tnode_t *row = node->child_first; row != NULL; row = row->sib_next) {
            int32_t row_height = 0;
            size_t column = 0;
            for (edit_tnode_t *cell = row->child_first; cell != NULL;
                 cell = cell->sib_next, ++column) {
                compute_intrinsic(tree, cell);
                edit_size_t size = intrinsic_to_outer(cell);
                while (node->table_ncols <= column) {
                    if (!columns_grow(tree, node)) {
                        return;
                    }
                    node->table_columns[node->table_ncols++] = 0;
                }
                if (size.width > node->table_columns[column]) {
                    node->table_columns[column] = size.width;
                }
                if (size.height > row_height) {
                    row_height = size.height;
                }
            }
            row->intrinsic_size.height = row_height;
        }
        size_t ncols = node->table_ncols;
        int32_t gap_w = node->table_gap.width * (int32_t)(ncols > 0 ? ncols - 1 : 0);
        int32_t total_inner = gap_w;
        for (size_t i = 0; i < ncols; ++i) {
            total_inner += node->table_columns[i];
        }
        int32_t total_width = 0;
        int32_t total_height = 0;
        for (edit_tnode_t *row = node->child_first; row != NULL; row = row->sib_next) {
            row->intrinsic_size.width = total_inner;
            row->intrinsic_set = true;
            edit_size_t size = intrinsic_to_outer(row);
            if (size.width > total_width) {
                total_width = size.width;
            }
            total_height += size.height;
        }
        size_t nrows = node->child_count;
        total_height += node->table_gap.height * (int32_t)(nrows > 0 ? nrows - 1 : 0);
        if (!node->intrinsic_set) {
            node->intrinsic_size.width = total_width;
            node->intrinsic_size.height = total_height;
            node->intrinsic_set = true;
        }
        return;
    }
    int32_t max_width = 0;
    int32_t total_height = 0;
    for (edit_tnode_t *child = node->child_first; child != NULL; child = child->sib_next) {
        compute_intrinsic(tree, child);
        edit_size_t size = intrinsic_to_outer(child);
        if (size.width > max_width) {
            max_width = size.width;
        }
        total_height += size.height;
    }
    if (!node->intrinsic_set) {
        node->intrinsic_size.width = max_width;
        node->intrinsic_size.height = total_height;
        node->intrinsic_set = true;
    }
}

static void layout_children(edit_tree_t *tree, edit_tnode_t *node, edit_rect_t clip);

static void layout_children(edit_tree_t *tree, edit_tnode_t *node, edit_rect_t clip) {
    if (node->child_first == NULL || edit_rect_is_empty(node->inner)) {
        return;
    }
    (void)tree;
    if (node->content_kind == 1) {
        int32_t width = node->inner.right - node->inner.left;
        int32_t x = node->inner.left;
        int32_t y = node->inner.top;
        for (edit_tnode_t *row = node->child_first; row != NULL; row = row->sib_next) {
            edit_size_t size = intrinsic_to_outer(row);
            size.width = width;
            row->outer.left = x;
            row->outer.top = y;
            row->outer.right = x + size.width;
            row->outer.bottom = y + size.height;
            row->outer = edit_rect_intersect(row->outer, node->inner);
            row->inner = row->outer;
            outer_to_inner(row, &row->inner);
            row->outer_clipped = edit_rect_intersect(row->outer, clip);
            row->inner_clipped = edit_rect_intersect(row->inner, clip);
            int32_t row_height = 0;
            size_t column = 0;
            for (edit_tnode_t *cell = row->child_first; cell != NULL;
                 cell = cell->sib_next, ++column) {
                edit_size_t csize = intrinsic_to_outer(cell);
                int32_t cw = (column < node->table_ncols) ? node->table_columns[column] : 0;
                csize.width = cw;
                cell->outer.left = x;
                cell->outer.top = y;
                cell->outer.right = x + csize.width;
                cell->outer.bottom = y + csize.height;
                cell->outer = edit_rect_intersect(cell->outer, node->inner);
                cell->inner = cell->outer;
                outer_to_inner(cell, &cell->inner);
                cell->outer_clipped = edit_rect_intersect(cell->outer, clip);
                cell->inner_clipped = edit_rect_intersect(cell->inner, clip);
                x += csize.width + node->table_gap.width;
                if (csize.height > row_height) {
                    row_height = csize.height;
                }
                layout_children(tree, cell, clip);
            }
            x = node->inner.left;
            y += row_height + node->table_gap.height;
        }
        return;
    }
    if (node->content_kind == 2) {
        edit_tnode_t *content = node->child_first;
        if (content == NULL) {
            return;
        }
        int32_t sx = node->inner.right - node->inner.left;
        int32_t sy = node->inner.bottom - node->inner.top;
        int32_t cx = sx;
        int32_t cy = content->intrinsic_size.height > sy ? content->intrinsic_size.height : sy;
        int32_t denom = cy - sy;
        int32_t oy = denom > 0 ? clamp32(node->scroll_offset.y, 0, denom) : 0;
        content->scroll_offset.x = 0;
        content->scroll_offset.y = oy;
        content->outer.left = node->inner.left;
        content->outer.top = node->inner.top - oy;
        content->outer.right = content->outer.left + cx;
        content->outer.bottom = content->outer.top + cy;
        content->inner = content->outer;
        outer_to_inner(content, &content->inner);
        content->outer_clipped = edit_rect_intersect(content->outer, node->inner_clipped);
        content->inner_clipped = edit_rect_intersect(content->inner, node->inner_clipped);
        layout_children(tree, content, content->inner_clipped);
        return;
    }
    int32_t width = node->inner.right - node->inner.left;
    int32_t x = node->inner.left;
    int32_t y = node->inner.top;
    for (edit_tnode_t *child = node->child_first; child != NULL; child = child->sib_next) {
        edit_size_t size = intrinsic_to_outer(child);
        int32_t remaining = width - size.width;
        if (remaining < 0) {
            remaining = 0;
        }
        int32_t off = 0;
        if (child->attributes.position == EDIT_POS_CENTER) {
            off = remaining / 2;
        } else if (child->attributes.position == EDIT_POS_RIGHT) {
            off = remaining;
        }
        child->outer.left = x + off;
        child->outer.right = child->outer.left +
                             (child->attributes.position == EDIT_POS_STRETCH ? width : size.width);
        child->outer.top = y;
        child->outer.bottom = y + size.height;
        child->outer = edit_rect_intersect(child->outer, node->inner);
        child->inner = child->outer;
        outer_to_inner(child, &child->inner);
        child->outer_clipped = edit_rect_intersect(child->outer, clip);
        child->inner_clipped = edit_rect_intersect(child->inner, clip);
        y += size.height;
    }
    for (edit_tnode_t *child = node->child_first; child != NULL; child = child->sib_next) {
        layout_children(tree, child, clip);
    }
}

void edit_tree_layout(edit_tree_t *tree, edit_rect_t viewport) {
    if (tree == NULL) {
        return;
    }
    for (edit_tnode_t *root = tree->root_first; root != NULL; root = root->sib_next) {
        compute_intrinsic(tree, root);
        if (root->attributes.float_attr.has_float) {
            int32_t x = 0;
            int32_t y = 0;
            if (root->parent != NULL) {
                x = root->parent->outer.left;
                y = root->parent->outer.top;
            }
            edit_size_t size = intrinsic_to_outer(root);
            float fx = root->attributes.float_attr.offset_x -
                       root->attributes.float_attr.gravity_x * (float)size.width;
            float fy = root->attributes.float_attr.offset_y -
                       root->attributes.float_attr.gravity_y * (float)size.height;
            x += sat_f32(fx);
            y += sat_f32(fy);
            root->outer.left = x;
            root->outer.top = y;
            root->outer.right = x + size.width;
            root->outer.bottom = y + size.height;
            root->outer = edit_rect_intersect(root->outer, viewport);
        } else {
            root->outer = viewport;
        }
        root->inner = root->outer;
        outer_to_inner(root, &root->inner);
        root->outer_clipped = root->outer;
        root->inner_clipped = root->inner;
        edit_rect_t outer = root->outer;
        layout_children(tree, root, outer);
    }
}
