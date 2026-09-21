#include "edit/vm.h"

#include <errno.h>
#include <sys/mman.h>

bool edit_vm_reserve(size_t size, uint8_t **out_base, edit_error_t *out_err) {
    if (out_base == NULL) {
        return false;
    }
    *out_base = NULL;
    if (size == 0) {
        return false;
    }
    void *p = mmap(NULL, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == NULL || p == MAP_FAILED) {
        if (out_err != NULL) {
            *out_err = edit_error_sys((uint32_t)ENOMEM);
        }
        return false;
    }
    *out_base = (uint8_t *)p;
    return true;
}

bool edit_vm_commit(uint8_t *base, size_t size, edit_error_t *out_err) {
    if (base == NULL || size == 0) {
        return false;
    }
    if (mprotect(base, size, PROT_READ | PROT_WRITE) != 0) {
        if (out_err != NULL) {
            *out_err = edit_error_sys((uint32_t)ENOMEM);
        }
        return false;
    }
    return true;
}

void edit_vm_release(uint8_t *base, size_t size) {
    if (base == NULL || size == 0) {
        return;
    }
    munmap(base, size);
}
