#define _XOPEN_SOURCE 600 // posix_openpt etc.; must precede headers
#define _DEFAULT_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include "edit/apperr.h"
#include "edit/tty.h"

static int checks = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #cond);                                     \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

// Opens a pty pair; leader reads/writes the terminal from outside.
static int open_pty(int *out_leader, int *out_follower) {
    int leader = posix_openpt(O_RDWR | O_NOCTTY);
    if (leader < 0) {
        return -1;
    }
    if (grantpt(leader) != 0 || unlockpt(leader) != 0) {
        close(leader);
        return -1;
    }
    char *name = ptsname(leader);
    if (name == NULL) {
        close(leader);
        return -1;
    }
    int follower = open(name, O_RDWR | O_NOCTTY);
    if (follower < 0) {
        close(leader);
        return -1;
    }
    struct winsize ws;
    memset(&ws, 0, sizeof ws);
    ws.ws_col = 80;
    ws.ws_row = 24;
    ioctl(follower, TIOCSWINSZ, &ws);
    *out_leader = leader;
    *out_follower = follower;
    return 0;
}

static void drain_leader(int leader) {
    int flags = fcntl(leader, F_GETFL);
    fcntl(leader, F_SETFL, flags | O_NONBLOCK);
    char buf[256];
    while (read(leader, buf, sizeof buf) > 0) {
    }
    fcntl(leader, F_SETFL, flags);
}

int main(void) {
    int leader = -1;
    int follower = -1;
    CHECK(open_pty(&leader, &follower) == 0);
    edit_tty_bind_fds(follower, follower);
    CHECK(edit_tty_stdin_redirected()); // bound fd differs from fd 0
    CHECK(edit_tty_switch_modes(NULL) == 0);

    // Raw write round-trips through the pty.
    CHECK(edit_tty_write((const uint8_t *)"hi", 2));
    CHECK(edit_tty_write(NULL, 0));
    char got[8] = {0};
    size_t total = 0;
    while (total < 2) {
        ssize_t n = read(leader, got + total, 2 - total);
        if (n <= 0) {
            break;
        }
        total += (size_t)n;
    }
    CHECK(total == 2 && memcmp(got, "hi", 2) == 0);

    // Timed read of pasted input.
    CHECK(write(leader, "abc", 3) == 3);
    uint8_t *out = NULL;
    size_t n = 0;
    CHECK(edit_tty_read(&out, &n, 2000) == EDIT_TTY_DATA);
    CHECK(n == 3 && memcmp(out, "abc", 3) == 0);
    free(out);
    out = NULL;

    // Timeout with no input.
    CHECK(edit_tty_read(&out, &n, 0) == EDIT_TTY_TIMEOUT);
    CHECK(out == NULL && n == 0);

    // Split UTF-8 sequences reassemble across reads.
    CHECK(write(leader, "\xC3", 1) == 1);
    CHECK(edit_tty_read(&out, &n, 2000) == EDIT_TTY_TIMEOUT);
    CHECK(write(leader, "\xA9", 1) == 1);
    CHECK(edit_tty_read(&out, &n, 2000) == EDIT_TTY_DATA);
    CHECK(n == 2 && memcmp(out, "\xC3\xA9", 2) == 0);
    free(out);
    out = NULL;

    // SIGWINCH injection prepends a size report.
    edit_tty_inject_resize();
    CHECK(write(leader, "z", 1) == 1);
    CHECK(edit_tty_read(&out, &n, 2000) == EDIT_TTY_DATA);
    CHECK(n == 11 && memcmp(out, "\x1b[8;24;80t", 10) == 0 && out[10] == 'z');
    free(out);
    out = NULL;

    // EOF (closed pipe) reads as CLOSED.
    {
        int pfd[2];
        CHECK(pipe(pfd) == 0);
        close(pfd[1]);
        edit_tty_bind_fds(pfd[0], follower);
        CHECK(edit_tty_read(&out, &n, 1000) == EDIT_TTY_CLOSED);
        close(pfd[0]);
    }
    edit_tty_bind_fds(follower, follower);

    // file_id identity.
    {
        uint64_t d1 = 0;
        uint64_t i1 = 0;
        uint64_t d2 = 0;
        uint64_t i2 = 0;
        CHECK(edit_tty_file_id(follower, &d1, &i1));
        CHECK(edit_tty_file_id(follower, &d2, &i2));
        CHECK(d1 == d2 && i1 == i2);
        int nullfd = open("/dev/null", O_RDONLY);
        uint64_t dn = 0;
        uint64_t in = 0;
        CHECK(edit_tty_file_id(nullfd, &dn, &in));
        CHECK(dn != d1 || in != i1);
        close(nullfd);
        CHECK(!edit_tty_file_id(-1, NULL, NULL));
    }

    // Language precedence: LANGUAGE wins, then LC_ALL, then LANG.
    {
        char *old_lang = getenv("LANGUAGE") != NULL ? strdup(getenv("LANGUAGE")) : NULL;
        char *old_all = getenv("LC_ALL") != NULL ? strdup(getenv("LC_ALL")) : NULL;
        char *old_l = getenv("LANG") != NULL ? strdup(getenv("LANG")) : NULL;
        unsetenv("LANGUAGE");
        unsetenv("LC_ALL");
        unsetenv("LANG");
        char **list = NULL;
        CHECK(edit_tty_languages(&list) == 0 && list == NULL);
        setenv("LANG", "en_US.UTF-8:fr", 1);
        CHECK(edit_tty_languages(&list) == 2);
        CHECK(strcmp(list[0], "en_US.UTF-8") == 0 && strcmp(list[1], "fr") == 0);
        for (size_t i = 0; list[i] != NULL; ++i) {
            free(list[i]);
        }
        free(list);
        setenv("LC_ALL", "C", 1);
        CHECK(edit_tty_languages(&list) == 1 && strcmp(list[0], "C") == 0);
        for (size_t i = 0; list[i] != NULL; ++i) {
            free(list[i]);
        }
        free(list);
        setenv("LANGUAGE", "de::en", 1);
        CHECK(edit_tty_languages(&list) == 2 && strcmp(list[0], "de") == 0);
        for (size_t i = 0; list[i] != NULL; ++i) {
            free(list[i]);
        }
        free(list);
        if (old_lang != NULL) {
            setenv("LANGUAGE", old_lang, 1);
        } else {
            unsetenv("LANGUAGE");
        }
        if (old_all != NULL) {
            setenv("LC_ALL", old_all, 1);
        } else {
            unsetenv("LC_ALL");
        }
        if (old_l != NULL) {
            setenv("LANG", old_l, 1);
        } else {
            unsetenv("LANG");
        }
        free(old_lang);
        free(old_all);
        free(old_l);
    }

    // Error text + not-found classification.
    {
        char buf[128];
        size_t need = edit_tty_error_text((uint32_t)ENOENT, buf, sizeof buf);
        CHECK(need > 9 && memcmp(buf, "Error 2: ", 9) == 0);
        CHECK(edit_tty_error_text((uint32_t)ENOENT, NULL, 0) == need);
        CHECK(edit_tty_is_not_found(edit_error_sys((uint32_t)ENOENT)));
        CHECK(!edit_tty_is_not_found(edit_error_sys((uint32_t)EACCES)));
        CHECK(!edit_tty_is_not_found(edit_error_app(0)));
    }

    // NULL safety.
    CHECK(edit_tty_read(NULL, NULL, 0) == EDIT_TTY_CLOSED);
    CHECK(!edit_tty_write(NULL, 5));
    edit_tty_deinit();
    drain_leader(leader);
    close(leader);
    close(follower);

    printf("test_tty: %d checks passed\n", checks);
    return 0;
}
