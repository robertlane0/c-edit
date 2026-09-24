#include "edit/tui.h"

#include <stdlib.h>
#include <string.h>

#include "edit/measure.h"
#include "edit/oklab.h"
#include "edit/uitext.h"

static void draw_borders(edit_fb_t *fb, edit_rect_t outer) {
    int32_t w = outer.right - outer.left;
    int32_t h = outer.bottom - outer.top;
    if (w < 2 || h < 2) {
        return;
    }
    // 3 bytes per glyph; heap for arbitrarily wide viewports.
    size_t row_bytes = (size_t)w * 3;
    char *top = (char *)malloc(row_bytes);
    char *mid = (char *)malloc(row_bytes);
    char *bottom = (char *)malloc(row_bytes);
    if (top == NULL || mid == NULL || bottom == NULL) {
        free(top);
        free(mid);
        free(bottom);
        return;
    }
    size_t k = 0;
    memcpy(top + k, "\xE2\x94\x8C", 3);
    k += 3;
    for (int32_t i = 0; i < w - 2; ++i) {
        memcpy(top + k, "\xE2\x94\x80", 3);
        k += 3;
    }
    memcpy(top + k, "\xE2\x94\x90", 3);
    k += 3;
    size_t m = 0;
    memcpy(mid + m, "\xE2\x94\x82", 3);
    m += 3;
    for (int32_t i = 0; i < w - 2; ++i) {
        mid[m++] = ' ';
    }
    memcpy(mid + m, "\xE2\x94\x82", 3);
    m += 3;
    size_t b = 0;
    memcpy(bottom + b, "\xE2\x94\x94", 3);
    b += 3;
    for (int32_t i = 0; i < w - 2; ++i) {
        memcpy(bottom + b, "\xE2\x94\x80", 3);
        b += 3;
    }
    memcpy(bottom + b, "\xE2\x94\x98", 3);
    b += 3;
    edit_fb_replace_text(fb, outer.top, outer.left, outer.right, top, k);
    for (int32_t y = outer.top + 1; y < outer.bottom - 1; ++y) {
        edit_fb_replace_text(fb, y, outer.left, outer.right, mid, m);
    }
    edit_fb_replace_text(fb, outer.bottom - 1, outer.left, outer.right, bottom, b);
    free(top);
    free(mid);
    free(bottom);
}

static void render_node(edit_tui_t *tui, edit_tnode_t *node) {
    edit_fb_t *fb = &tui->framebuffer;
    if (node->outer_clipped.left >= node->outer_clipped.right ||
        node->outer_clipped.top >= node->outer_clipped.bottom) {
        return;
    }
    edit_rect_t outer_clipped = node->outer_clipped;

    if (node->attributes.bordered) {
        draw_borders(fb, outer_clipped);
    }
    if (node->attributes.float_attr.has_float &&
        (node->attributes.bg & 0xFF000000U) == 0xFF000000U) {
        if (!node->attributes.bordered) {
            int32_t w = outer_clipped.right - outer_clipped.left;
            if (w > 0) {
                char *fill = (char *)malloc((size_t)w + 1);
                if (fill != NULL) {
                    memset(fill, ' ', (size_t)w);
                    for (int32_t y = outer_clipped.top; y < outer_clipped.bottom; ++y) {
                        edit_fb_replace_text(fb, y, outer_clipped.left, outer_clipped.right, fill,
                                             (size_t)w);
                    }
                    free(fill);
                }
            }
        }
    }

    edit_fb_blend_bg(fb, outer_clipped, node->attributes.bg);
    edit_fb_blend_fg(fb, outer_clipped, node->attributes.fg);
    if (node->attributes.reverse) {
        edit_fb_reverse(fb, outer_clipped);
    }

    edit_rect_t inner = node->inner;
    edit_rect_t inner_clipped = node->inner_clipped;
    if (inner_clipped.left >= inner_clipped.right || inner_clipped.top >= inner_clipped.bottom) {
        return;
    }

    if (node->content_kind == 4) {
        if (node->modal_title != NULL) {
            edit_fb_replace_text(fb, node->outer.top, node->outer.left + 2, node->outer.right - 1,
                                 node->modal_title, strlen(node->modal_title));
        }
    } else if (node->content_kind == 3) {
        // Styled text via a borrowed view (render never frees).
        edit_text_content_t view;
        memset(&view, 0, sizeof view);
        view.text = (char *)node->text_ptr;
        view.len = node->text_len;
        view.chunks = node->text_chunks;
        view.nchunks = node->text_nchunks;
        view.overflow = node->text_overflow;
        size_t actual = edit_text_measure(node->text_ptr, node->text_len);
        edit_text_render(&view, inner, actual, fb);
    } else if (node->content_kind == 5) {
        edit_tbuf_t *tb = NULL;
        if (node->ta_single_line) {
            // Editline editors live in the TUI cache, keyed by node id.
            for (size_t i = 0; i < tui->tbuf_cache_len; ++i) {
                if (tui->tbuf_cache[i].node_id == node->id) {
                    tb = &tui->tbuf_cache[i].editor->tbuf;
                    break;
                }
            }
        } else if (node->ta_buffer != NULL) {
            tb = &((edit_shared_tbuf_t *)node->ta_buffer)->tbuf;
        }
        if (tb != NULL) {
            edit_rect_t dest = {inner_clipped.left, inner_clipped.top, inner_clipped.right,
                                inner_clipped.bottom};
            if (!node->ta_single_line) {
                dest.right -= 1;
            }
            int32_t xmax = 0;
            edit_point_t origin = {node->ta_scroll.x, node->ta_scroll.y};
            if (edit_tbuf_render(tb, origin, dest, node->ta_has_focus, fb, &xmax)) {
                node->ta_xmax = xmax;
            }
            if (!node->ta_single_line) {
                edit_rect_t track = {inner_clipped.right - 1, inner_clipped.top,
                                     inner_clipped.right, inner_clipped.bottom};
                int32_t content_h = edit_tbuf_visual_lines(tb) + inner.bottom - inner.top - 1;
                node->ta_thumb =
                    edit_fb_scrollbar(fb, inner_clipped, track, node->ta_scroll.y, content_h);
            }
        }
    } else if (node->content_kind == 2) {
        edit_tnode_t *content = node->child_first;
        if (content != NULL) {
            edit_rect_t track = {inner.right, inner.top, inner.right + 1, inner.bottom};
            node->scroll_thumb = edit_fb_scrollbar(fb, outer_clipped, track, node->scroll_offset.y,
                                                   content->intrinsic_size.height);
        }
    }

    for (edit_tnode_t *child = node->child_first; child != NULL; child = child->sib_next) {
        render_node(tui, child);
    }
}

void edit_tui_draw(edit_tui_t *t) {
    if (t == NULL) {
        return;
    }
    for (edit_tnode_t *root = t->prev_tree.root_first; root != NULL; root = root->sib_next) {
        render_node(t, root);
    }
}
