#include <errno.h>
#include <stdint.h>
#include <stdio.h>

#include "edit/apperr.h"
#include "edit/vm.h"

static int checks = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #cond);                                     \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

int main(void) {
    // Reserve is PROT_NONE: reservation succeeds, first page uncommitted.
    uint8_t *base = NULL;
    edit_error_t err = edit_error_app(99);
    CHECK(edit_vm_reserve(1 << 20, &base, &err));
    CHECK(base != NULL);

    // Commit, write, release round-trip.
    CHECK(edit_vm_commit(base, 4096, &err));
    for (size_t i = 0; i < 4096; ++i) {
        base[i] = (uint8_t)(i & 0xFF);
    }
    for (size_t i = 0; i < 4096; ++i) {
        CHECK(base[i] == (uint8_t)(i & 0xFF));
    }
    // Committing an already-committed range is fine.
    CHECK(edit_vm_commit(base, 4096, NULL));
    edit_vm_release(base, 1 << 20);

    // Absurd size fails with Sys(ENOMEM), matching Rust.
    uint8_t *bad = (uint8_t *)0x1234;
    CHECK(!edit_vm_reserve((size_t)-1, &bad, &err));
    CHECK(bad == NULL);
    CHECK(edit_error_equal(err, edit_error_sys((uint32_t)ENOMEM)));
    // NULL out_err still reports failure.
    CHECK(!edit_vm_reserve((size_t)-1, &bad, NULL));

    printf("test_vm: %d checks passed\n", checks);
    return 0;
}
