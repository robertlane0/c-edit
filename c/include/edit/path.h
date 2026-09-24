#ifndef EDIT_PATH_H
#define EDIT_PATH_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Unix absolute paths only (Windows support lands with the sys slice).
// snprintf-style: NUL-terminates within cap, returns full length excl. NUL.
// Collapses //, drops trailing /, removes ., resolves .. (never above /).
// Returns needed length, or (size_t)(-1) on NULL args or non-absolute path.
// dst == NULL queries the length.
size_t edit_path_normalize(char *dst, size_t cap, const char *path);

#ifdef __cplusplus
}
#endif

#endif
