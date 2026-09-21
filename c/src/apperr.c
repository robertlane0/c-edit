#include "edit/apperr.h"

const edit_error_t EDIT_APP_ICU_MISSING = {EDIT_ERR_APP, 0};

edit_error_t edit_error_app(uint32_t code) {
    edit_error_t e = {EDIT_ERR_APP, code};
    return e;
}

edit_error_t edit_error_icu(uint32_t code) {
    edit_error_t e = {EDIT_ERR_ICU, code};
    return e;
}

edit_error_t edit_error_sys(uint32_t code) {
    edit_error_t e = {EDIT_ERR_SYS, code};
    return e;
}

bool edit_error_equal(edit_error_t a, edit_error_t b) {
    return a.kind == b.kind && a.code == b.code;
}
