#ifndef EDIT_VM_H
#define EDIT_VM_H

#include <stddef.h>
#include <stdint.h>

#include "edit/apperr.h"

#ifdef __cplusplus
extern "C" {
#endif

// POSIX virtual memory (cf. Rust sys::unix virtual_*).
// Reserve address space without committing; commit with R/W; release all.
// Errors report Sys(ENOMEM), matching Rust. Caller owns the reservation.
bool edit_vm_reserve(size_t size, uint8_t **out_base, edit_error_t *out_err);
bool edit_vm_commit(uint8_t *base, size_t size, edit_error_t *out_err);
void edit_vm_release(uint8_t *base, size_t size);

#ifdef __cplusplus
}
#endif

#endif
