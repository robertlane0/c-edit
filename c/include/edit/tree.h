#ifndef EDIT_TREE_H
#define EDIT_TREE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "edit/arena.h"
#include "edit/helpers.h"

// Styled text chunk + overflow live here (tree content model).
typedef enum {
    EDIT_OVF_CLIP,
    EDIT_OVF_HEAD,   // ellipsis first ("…tail")
    EDIT_OVF_MIDDLE, // ellipsis inside ("he…lo")
    EDIT_OVF_TAIL,   // ellipsis last ("hea…")
} edit_overflow_t;

typedef struct {
    size_t offset;
    uint32_t fg;
    uint8_t attr;
} edit_text_chunk_t;

#ifdef __cplusplus
extern "C" {
#endif

// UI tree core (cf. Rust tui::Tree/Node/NodeMap). Nodes live in a caller
// arena (no destructors needed); links are stable pointers. Single-threaded.
#define EDIT_ROOT_ID UINT64_C(0x14057B7EF767814F)

typedef enum {
    EDIT_POS_STRETCH,
    EDIT_POS_LEFT,
    EDIT_POS_CENTER,
    EDIT_POS_RIGHT,
} edit_position_t;

typedef struct {
    bool has_float;
    float gravity_x;
    float gravity_y;
    float offset_x;
    float offset_y;
} edit_float_attr_t;

typedef struct {
    edit_float_attr_t float_attr;
    edit_position_t position;
    edit_rect_t padding;
    uint32_t bg;
    uint32_t fg;
    bool reverse;
    bool bordered;
    bool focusable;
    bool focus_well;
    bool focus_void;
} edit_node_attr_t;

typedef struct edit_tnode {
    struct edit_tnode *prev;
    struct edit_tnode *next;
    struct edit_tnode *stack_parent;
    uint64_t id;
    const char *classname;
    struct edit_tnode *parent;
    size_t depth;
    struct edit_tnode *sib_prev;
    struct edit_tnode *sib_next;
    struct edit_tnode *child_first;
    struct edit_tnode *child_last;
    size_t child_count;
    edit_node_attr_t attributes;
    // Content kinds: 0 none, 1 table, 2 scrollarea, 3 text, 4 modal,
    // 5 textarea, 6 list.
    int content_kind;
    // Text content (arena storage, label builders).
    const char *text_ptr;
    size_t text_len;
    size_t text_cap;
    edit_text_chunk_t *text_chunks;
    size_t text_nchunks;
    size_t text_chunkcap;
    edit_overflow_t text_overflow;
    // Modal title (arena storage, NULL when none/empty).
    const char *modal_title;
    // Textarea content (buffer borrowed, state retained per node id).
    void *ta_buffer; // edit_shared_tbuf_t*, borrowed
    edit_point_t ta_scroll;
    int32_t ta_drag_start;
    int32_t ta_xmax;
    int32_t ta_thumb;
    int32_t ta_preferred;
    bool ta_single_line;
    bool ta_has_focus;
    // List content: selected id + selected node (borrowed).
    uint64_t list_selected;
    struct edit_tnode *list_selected_node;
    // Table: column widths (arena vec) + cell gap.
    int32_t *table_columns;
    size_t table_ncols;
    size_t table_capcols;
    edit_size_t table_gap;
    // Scrollarea: retained scroll state.
    edit_point_t scroll_offset;
    int32_t scroll_drag_start;
    int32_t scroll_thumb;
    edit_size_t intrinsic_size;
    bool intrinsic_set;
    edit_rect_t outer;
    edit_rect_t inner;
    edit_rect_t outer_clipped;
    edit_rect_t inner_clipped;
} edit_tnode_t;

typedef struct {
    edit_arena_t *arena;
    edit_tnode_t *tail;
    edit_tnode_t *root_first;
    edit_tnode_t *root_last;
    edit_tnode_t *last_node;
    edit_tnode_t *current_node;
    size_t count;
    uint64_t checksum;
    // Debug duplicate-ID tracking (always on; cheap for small trees).
    uint64_t *seen_ids;
    size_t seen_len;
    size_t seen_cap;
    uint64_t id_mixin;
    bool has_mixin;
} edit_tree_t;

void edit_tree_init(edit_tree_t *t, edit_arena_t *arena);
void edit_tree_destroy(edit_tree_t *t);
// Begins a child block with parent-derived ID (cf. Context::block_begin).
// Returns NULL on OOM (or duplicate ID in any build; asserts in debug).
edit_tnode_t *edit_tree_block_begin(edit_tree_t *t, const char *classname);
void edit_tree_block_end(edit_tree_t *t);
// Extra uniqueness for the next block (lists of same-class items).
void edit_tree_id_mixin(edit_tree_t *t, uint64_t id);
// Detaches node to the root list under anchor (NULL = depth 0).
void edit_tree_to_root(edit_tree_t *t, edit_tnode_t *node, edit_tnode_t *anchor);

typedef enum {
    EDIT_VISIT_CONTINUE,
    EDIT_VISIT_SKIP,
    EDIT_VISIT_STOP,
} edit_visit_t;

typedef edit_visit_t (*edit_visit_fn)(edit_tnode_t *node, void *ctx);
// Depth-first wraparound traversal from start within root.
void edit_tree_visit(edit_tnode_t *root, edit_tnode_t *start, bool forward, edit_visit_fn cb,
                     void *ctx);

// Layout (cf. Node::compute_intrinsic_size/layout_children + frame driver).
// Content kinds for layout: 0 none, 1 table, 2 scrollarea.
void edit_tree_make_table(edit_tnode_t *node);
void edit_tree_make_scrollarea(edit_tnode_t *node);
void edit_tree_layout(edit_tree_t *tree, edit_rect_t viewport);

typedef struct {
    edit_tnode_t **slots; // owned heap array (NULL = empty)
    size_t slots_count;
    size_t shift;
    uint64_t mask;
} edit_nodemap_t;

// 4x slots for 25% fill; false on OOM. Heap-owned; destroy frees.
bool edit_nodemap_build(edit_nodemap_t *m, edit_tree_t *tree);
void edit_nodemap_destroy(edit_nodemap_t *m);
edit_tnode_t *edit_nodemap_get(edit_nodemap_t *m, uint64_t id);

#ifdef __cplusplus
}
#endif

#endif
