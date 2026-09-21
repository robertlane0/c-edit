#ifndef EDIT_APPERR_H
#define EDIT_APPERR_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EDIT_ERR_APP = 0,
    EDIT_ERR_ICU = 1,
    EDIT_ERR_SYS = 2,
} edit_err_kind_t;

typedef struct {
    edit_err_kind_t kind;
    uint32_t code;
} edit_error_t;

// Missing ICU data file.
extern const edit_error_t EDIT_APP_ICU_MISSING;

edit_error_t edit_error_app(uint32_t code);
edit_error_t edit_error_icu(uint32_t code);
edit_error_t edit_error_sys(uint32_t code);
bool edit_error_equal(edit_error_t a, edit_error_t b);

#ifdef __cplusplus
}
#endif

#endif
