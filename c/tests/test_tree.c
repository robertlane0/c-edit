#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "edit/arena.h"
#include "edit/tree.h"

static int checks = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #cond);                                     \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

typedef struct {
    uint64_t ids[16];
    size_t n;
    int stop_after; // -1 = never
} visit_log_t;

static edit_visit_t logger(edit_tnode_t *node, void *ctx) {
    visit_log_t *log = (visit_log_t *)ctx;
    if (log->n < 16) {
        log->ids[log->n++] = node->id;
    }
    if (log->stop_after >= 0 && (int)log->n > log->stop_after) {
        return EDIT_VISIT_STOP;
    }
    return EDIT_VISIT_CONTINUE;
}

static edit_visit_t skipper(edit_tnode_t *node, void *ctx) {
    visit_log_t *log = (visit_log_t *)ctx;
    if (log->n < 16) {
        log->ids[log->n++] = node->id;
    }
    // Skip the first child subtree encountered.
    return log->n == 1 ? EDIT_VISIT_SKIP : EDIT_VISIT_CONTINUE;
}

int main(void) {
    edit_arena_t arena;
    memset(&arena, 0, sizeof arena);
    CHECK(edit_arena_init(&arena, 65536) == 0);

    // IDs from Rust hash_str chains (root/menu/item...).
    edit_tree_t t;
    edit_tree_init(&t, &arena);
    CHECK(t.count == 1 && t.checksum == EDIT_ROOT_ID);
    CHECK(t.root_first == t.root_last && t.root_first->depth == 0);

    edit_tnode_t *menu = edit_tree_block_begin(&t, "menu");
    CHECK(menu != NULL && menu->id == UINT64_C(0xCF8F8E7EAE4430F1));
    CHECK(menu->depth == 1 && menu->parent == t.root_first);
    CHECK(t.current_node == menu && t.count == 2);

    edit_tnode_t *item = edit_tree_block_begin(&t, "item");
    CHECK(item != NULL && item->id == UINT64_C(0x1F570957B687E47F));
    edit_tree_block_end(&t);
    CHECK(t.current_node == menu);
    edit_tnode_t *item2 = edit_tree_block_begin(&t, "item2");
    CHECK(item2 != NULL && item2->id == UINT64_C(0x5AB478501BEF4E52));
    CHECK(menu->child_count == 2);
    CHECK(menu->child_first == item && menu->child_last == item2);
    CHECK(item->sib_next == item2 && item2->sib_prev == item);
    CHECK(t.checksum == UINT64_C(0x14729E9D505ADFE3));
    edit_tree_block_end(&t);
    edit_tree_block_end(&t);
    CHECK(t.current_node == t.root_first);

    // Mixin IDs (Rust hash with NE bytes).
    edit_tree_id_mixin(&t, 7);
    edit_tnode_t *m = edit_tree_block_begin(&t, "menu");
    // Mixin applies under root here (current is root after pops).
    CHECK(m != NULL);
    (void)m;

    // NodeMap lookups incl. misses.
    edit_nodemap_t map;
    memset(&map, 0, sizeof map);
    CHECK(edit_nodemap_build(&map, &t));
    CHECK(edit_nodemap_get(&map, EDIT_ROOT_ID) == t.root_first);
    CHECK(edit_nodemap_get(&map, menu->id) == menu);
    CHECK(edit_nodemap_get(&map, item2->id) == item2);
    CHECK(edit_nodemap_get(&map, UINT64_C(0xDEADBEEF)) == NULL);
    edit_nodemap_destroy(&map);

    // Forward visit from root covers all in DFS order.
    {
        visit_log_t log;
        memset(&log, 0, sizeof log);
        log.stop_after = -1;
        edit_tree_visit(t.root_first, t.root_first, true, logger, &log);
        CHECK(log.n == t.count);
        CHECK(log.ids[0] == EDIT_ROOT_ID);
    }
    // Backward visit ends at root from the tail.
    {
        visit_log_t log;
        memset(&log, 0, sizeof log);
        log.stop_after = -1;
        edit_tree_visit(t.root_first, t.tail, false, logger, &log);
        CHECK(log.n == t.count);
        CHECK(log.ids[log.n - 1] == EDIT_ROOT_ID);
    }
    // Stop early.
    {
        visit_log_t log;
        memset(&log, 0, sizeof log);
        log.stop_after = 1;
        edit_tree_visit(t.root_first, t.root_first, true, logger, &log);
        CHECK(log.n == 2);
    }
    // Skip children.
    {
        visit_log_t log;
        memset(&log, 0, sizeof log);
        edit_tree_visit(t.root_first, t.root_first, true, skipper, &log);
        CHECK(log.n >= 1 && log.ids[0] == EDIT_ROOT_ID);
    }
    // Subtree visit stays within bounds (menu subtree).
    {
        visit_log_t log;
        memset(&log, 0, sizeof log);
        log.stop_after = -1;
        edit_tree_visit(menu, menu, true, logger, &log);
        CHECK(log.n == 3); // menu + 2 items
    }

    // Move a node to the root list.
    {
        size_t before = menu->child_count;
        edit_tree_to_root(&t, item, NULL);
        CHECK(menu->child_count == before - 1);
        CHECK(item->parent == NULL && item->depth == 0);
        CHECK(menu->child_first == item2);
        CHECK(t.root_last == item);
        // Map still resolves it (rebuilt).
        edit_nodemap_t map2;
        memset(&map2, 0, sizeof map2);
        CHECK(edit_nodemap_build(&map2, &t));
        CHECK(edit_nodemap_get(&map2, item->id) == item);
        edit_nodemap_destroy(&map2);
        // Backward visit from the moved tail still terminates.
        // (The detached root is unreachable from here: m, menu, item2, root.)
        visit_log_t log;
        memset(&log, 0, sizeof log);
        log.stop_after = -1;
        edit_tree_visit(t.root_first, t.tail, false, logger, &log);
        CHECK(log.n == t.count - 1);
    }

    // NULL safety.
    edit_tree_init(NULL, NULL);
    edit_tree_destroy(NULL);
    CHECK(edit_tree_block_begin(NULL, "x") == NULL);
    edit_tree_block_end(NULL);
    edit_tree_id_mixin(NULL, 1);
    edit_tree_to_root(NULL, NULL, NULL);
    edit_tree_visit(NULL, NULL, true, NULL, NULL);
    CHECK(!edit_nodemap_build(NULL, NULL));
    edit_nodemap_destroy(NULL);
    CHECK(edit_nodemap_get(NULL, 0) == NULL);

    edit_tree_destroy(&t);
    edit_arena_destroy(&arena);
    printf("test_tree: %d checks passed\n", checks);
    return 0;
}
