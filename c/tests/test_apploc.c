#define _DEFAULT_SOURCE // setenv/unsetenv; must precede headers

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "edit/apploc.h"

static int checks = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #cond);                                     \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static void clear_env(void) {
    unsetenv("LANGUAGE");
    unsetenv("LC_ALL");
    unsetenv("LANG");
}

int main(void) {
    // 56 ids + Count sentinel (matches Rust LocId).
    CHECK(EDIT_LOC_COUNT == 56);

    clear_env();
    edit_loc_init();
    CHECK(strcmp(edit_loc(EDIT_LOC_FILE), "File") == 0);
    CHECK(strcmp(edit_loc(EDIT_LOC_OK), "Ok") == 0);

    setenv("LANGUAGE", "de", 1);
    edit_loc_init();
    CHECK(strcmp(edit_loc(EDIT_LOC_FILE), "Datei") == 0);
    CHECK(strcmp(edit_loc(EDIT_LOC_CTRL), "Strg") == 0);

    // First set variable wins; unknown pieces skipped.
    setenv("LANGUAGE", "xx:de", 1);
    edit_loc_init();
    CHECK(strcmp(edit_loc(EDIT_LOC_FILE), "Datei") == 0);

    // LANGUAGE beats LANG.
    setenv("LANGUAGE", "fr", 1);
    setenv("LANG", "de", 1);
    edit_loc_init();
    CHECK(strcmp(edit_loc(EDIT_LOC_FILE), "Fichier") == 0);

    // LC_ALL beats LANG when LANGUAGE unset.
    clear_env();
    setenv("LC_ALL", "ja", 1);
    setenv("LANG", "de", 1);
    edit_loc_init();
    CHECK(strcmp(edit_loc(EDIT_LOC_YES), "\xE3\x81\xAF\xE3\x81\x84") == 0); // はい

    // Region suffixes do not match (mirrors Rust exact matching).
    clear_env();
    setenv("LANG", "de_DE.UTF-8", 1);
    edit_loc_init();
    CHECK(strcmp(edit_loc(EDIT_LOC_FILE), "File") == 0);

    // zh variants map to distinct columns (OK: 确定 vs 確定).
    clear_env();
    setenv("LANG", "zh", 1);
    edit_loc_init();
    const char *zh = edit_loc(EDIT_LOC_OK);
    setenv("LANG", "zh-hant", 1);
    edit_loc_init();
    const char *hant = edit_loc(EDIT_LOC_OK);
    CHECK(strcmp(zh, hant) != 0);

    // pt-br with dash.
    clear_env();
    setenv("LANG", "pt-br", 1);
    edit_loc_init();
    CHECK(strcmp(edit_loc(EDIT_LOC_FILE), "Arquivo") == 0);

    // Out-of-range ids are safe.
    CHECK(strcmp(edit_loc((edit_loc_id_t)-1), "") == 0);
    CHECK(strcmp(edit_loc(EDIT_LOC_COUNT), "") == 0);

    clear_env();
    printf("test_apploc: %d checks passed\n", checks);
    return 0;
}
