#include "edit/tree.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "edit/hash.h"

void edit_tree_init(edit_tree_t *t, edit_arena_t *arena) {
    if (t == NULL) {
        return;
    }
    memset(t, 0, sizeof *t);
    t->arena = arena;
    if (arena == NULL) {
        return;
    }
    void *mem = NULL;
    if (!edit_arena_alloc(arena, sizeof(edit_tnode_t), _Alignof(edit_tnode_t), &mem)) {
        return;
    }
    edit_tnode_t *root = (edit_tnode_t *)mem;
    memset(root, 0, sizeof *root);
    root->id = EDIT_ROOT_ID;
    root->classname = "root";
    root->attributes.focusable = true;
    root->attributes.focus_well = true;
    t->tail = root;
    t->root_first = root;
    t->root_last = root;
    t->last_node = root;
    t->current_node = root;
    t->count = 1;
    t->checksum = EDIT_ROOT_ID;
}

void edit_tree_destroy(edit_tree_t *t) {
    if (t == NULL) {
        return;
    }
    free(t->seen_ids);
    t->seen_ids = NULL;
    t->seen_len = 0;
    t->seen_cap = 0;
}

// Duplicate-ID tracking (Rust panics in debug; C asserts + refuses).
static bool seen_insert(edit_tree_t *t, uint64_t id) {
    for (size_t i = 0; i < t->seen_len; ++i) {
        if (t->seen_ids[i] == id) {
            assert(false && "Duplicate node ID");
            return false;
        }
    }
    if (t->seen_len == t->seen_cap) {
        size_t grown = t->seen_cap != 0 ? t->seen_cap * 2 : 16;
        uint64_t *nb = (uint64_t *)realloc(t->seen_ids, grown * sizeof *nb);
        if (nb == NULL) {
            return false;
        }
        t->seen_ids = nb;
        t->seen_cap = grown;
    }
    t->seen_ids[t->seen_len++] = id;
    return true;
}

edit_tnode_t *edit_tree_block_begin(edit_tree_t *t, const char *classname) {
    if (t == NULL || t->arena == NULL || t->current_node == NULL || classname == NULL) {
        return NULL;
    }
    uint64_t id = edit_hash_str(t->current_node->id, classname);
    if (t->has_mixin) {
        uint8_t mix[8];
        memcpy(mix, &t->id_mixin, sizeof mix); // native order, like to_ne_bytes
        id = edit_hash(id, mix, 8);
        t->has_mixin = false;
    }
    if (!seen_insert(t, id)) {
        return NULL;
    }
    void *mem = NULL;
    if (!edit_arena_alloc(t->arena, sizeof(edit_tnode_t), _Alignof(edit_tnode_t), &mem)) {
        // Roll back the ID reservation.
        t->seen_len -= 1;
        return NULL;
    }
    edit_tnode_t *node = (edit_tnode_t *)mem;
    memset(node, 0, sizeof *node);
    node->id = id;
    node->classname = classname;

    node->parent = t->current_node;
    node->stack_parent = t->current_node;
    node->sib_prev = t->current_node->child_last;
    node->depth = t->current_node->depth + 1;
    if (t->current_node->child_last != NULL) {
        t->current_node->child_last->sib_next = node;
    }
    if (t->current_node->child_first == NULL) {
        t->current_node->child_first = node;
    }
    t->current_node->child_last = node;
    t->current_node->child_count += 1;

    node->prev = t->tail;
    if (t->tail != NULL) {
        t->tail->next = node;
    }
    t->tail = node;
    t->last_node = node;
    t->current_node = node;
    t->count += 1;
    t->checksum = edit_wymix(t->checksum, id);
    return node;
}

void edit_tree_block_end(edit_tree_t *t) {
    if (t == NULL || t->current_node == NULL || t->current_node->stack_parent == NULL) {
        assert(t == NULL || t->current_node == NULL || t->current_node->stack_parent != NULL);
        return;
    }
    t->last_node = t->current_node;
    t->current_node = t->current_node->stack_parent;
}

void edit_tree_id_mixin(edit_tree_t *t, uint64_t id) {
    if (t == NULL) {
        return;
    }
    t->id_mixin = id;
    t->has_mixin = true;
}

void edit_tree_to_root(edit_tree_t *t, edit_tnode_t *node, edit_tnode_t *anchor) {
    if (t == NULL || node == NULL || node->parent == NULL) {
        return;
    }
    if (node->sib_prev != NULL) {
        node->sib_prev->sib_next = node->sib_next;
    }
    if (node->sib_next != NULL) {
        node->sib_next->sib_prev = node->sib_prev;
    }
    edit_tnode_t *parent = node->parent;
    if (parent->child_first == node) {
        parent->child_first = node->sib_next;
    }
    if (parent->child_last == node) {
        parent->child_last = node->sib_prev;
    }
    if (parent->child_count > 0) {
        parent->child_count -= 1;
    }
    node->parent = anchor;
    node->depth = anchor != NULL ? anchor->depth + 1 : 0;
    node->sib_prev = t->root_last;
    node->sib_next = NULL;
    if (t->root_last != NULL) {
        t->root_last->sib_next = node;
    }
    t->root_last = node;
}

void edit_tree_visit(edit_tnode_t *root, edit_tnode_t *start, bool forward, edit_visit_fn cb,
                     void *ctx) {
    if (root == NULL || start == NULL || cb == NULL) {
        return;
    }
    size_t root_depth = root->depth;
    edit_tnode_t *node = start;
    for (;;) {
        bool descend = true;
        edit_visit_t ctl = cb(node, ctx);
        if (ctl == EDIT_VISIT_STOP) {
            return;
        }
        if (ctl == EDIT_VISIT_CONTINUE) {
            edit_tnode_t *child = forward ? node->child_first : node->child_last;
            if (child != NULL) {
                node = child;
                descend = false;
            }
        }
        if (descend) {
            for (;;) {
                if (node->depth <= root_depth) {
                    goto next;
                }
                edit_tnode_t *sib = forward ? node->sib_next : node->sib_prev;
                if (sib != NULL) {
                    node = sib;
                    break;
                }
                if (node->parent == NULL) {
                    goto next;
                }
                node = node->parent;
            }
        }
    next:
        if (node == start) {
            return;
        }
    }
}

// Floor log2 (mirrors usize::ilog2); v == 0 yields 0.
static size_t ilog2_floor(size_t v) {
    size_t w = 0;
    while ((v >> (w + 1)) > 0) {
        ++w;
    }
    return w;
}

bool edit_nodemap_build(edit_nodemap_t *m, edit_tree_t *tree) {
    if (m == NULL) {
        return false;
    }
    memset(m, 0, sizeof *m);
    if (tree == NULL || tree->root_first == NULL) {
        return false;
    }
    // 4x slots for 25% fill; width = floor log2, min 1.
    size_t need = 4 * tree->count + 1;
    size_t width = ilog2_floor(need);
    if (width < 1) {
        width = 1;
    }
    if (width >= 8 * sizeof(size_t)) {
        return false;
    }
    size_t slots = (size_t)1 << width;
    edit_tnode_t **tab = (edit_tnode_t **)calloc(slots, sizeof *tab);
    if (tab == NULL) {
        return false;
    }
    size_t shift = 64 - width;
    uint64_t mask = (uint64_t)(slots - 1);
    edit_tnode_t *node = tree->root_first;
    for (;;) {
        uint64_t slot = (node->id >> shift) & mask;
        while (tab[slot] != NULL) {
            slot = (slot + 1) & mask;
        }
        tab[slot] = node;
        if (node->next == NULL) {
            break;
        }
        node = node->next;
    }
    m->slots = tab;
    m->slots_count = slots;
    m->shift = shift;
    m->mask = mask;
    return true;
}

void edit_nodemap_destroy(edit_nodemap_t *m) {
    if (m == NULL) {
        return;
    }
    free(m->slots);
    memset(m, 0, sizeof *m);
}

edit_tnode_t *edit_nodemap_get(edit_nodemap_t *m, uint64_t id) {
    if (m == NULL || m->slots == NULL) {
        return NULL;
    }
    uint64_t slot = (id >> m->shift) & m->mask;
    for (;;) {
        edit_tnode_t *node = m->slots[slot];
        if (node == NULL) {
            return NULL;
        }
        if (node->id == id) {
            return node;
        }
        slot = (slot + 1) & m->mask;
    }
}
