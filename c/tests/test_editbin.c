#define _XOPEN_SOURCE 600 // posix_openpt; must precede headers
#define _DEFAULT_SOURCE
#define _GNU_SOURCE // memmem

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

static int checks = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #cond);                                     \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

#define EDIT_BIN "build/edit"

// Captured leader output.
static char capture[1 << 20];
static size_t capture_len;

// Read with an overall deadline (ms); appends to capture. True if `want`
// appears before the deadline.
static bool read_until(int leader, const char *want, int deadline_ms) {
    size_t want_len = want != NULL ? strlen(want) : 0;
    int waited = 0;
    while (waited < deadline_ms) {
        if (want != NULL && want_len > 0 && capture_len >= want_len) {
            for (size_t i = 0; i + want_len <= capture_len; ++i) {
                if (memcmp(capture + i, want, want_len) == 0) {
                    return true;
                }
            }
        }
        struct pollfd pfd;
        pfd.fd = leader;
        pfd.events = POLLIN;
        pfd.revents = 0;
        int rc = poll(&pfd, 1, 100);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (rc == 0) {
            waited += 100;
            continue;
        }
        char buf[4096];
        ssize_t n = read(leader, buf, sizeof buf);
        if (n <= 0) {
            waited += 100;
            continue;
        }
        size_t room = sizeof capture - capture_len - 1;
        size_t k = (size_t)n < room ? (size_t)n : room;
        memcpy(capture + capture_len, buf, k);
        capture_len += k;
    }
    return false;
}

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

// Spawns EDIT_BIN with the follower as stdio; returns child pid.
static pid_t spawn(int follower, char *const argv[]) {
    pid_t pid = fork();
    if (pid != 0) {
        return pid;
    }
    dup2(follower, STDIN_FILENO);
    dup2(follower, STDOUT_FILENO);
    dup2(follower, STDERR_FILENO);
    if (follower > STDERR_FILENO) {
        close(follower);
    }
    execv(argv[0], argv);
    _exit(127);
}

static bool wait_exit(pid_t pid, int deadline_ms, int *out_status) {
    int waited = 0;
    while (waited < deadline_ms) {
        pid_t got = waitpid(pid, out_status, WNOHANG);
        if (got == pid) {
            return true;
        }
        if (got < 0 && errno != EINTR) {
            return false;
        }
        usleep(50000);
        waited += 50;
    }
    return false;
}

static void full_write(int fd, const char *s, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(fd, s + off, n - off);
        if (w <= 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        off += (size_t)w;
    }
}

int main(void) {
    // --help under a pty (needs isatty for tty_init).
    {
        int leader = -1, follower = -1;
        CHECK(open_pty(&leader, &follower) == 0);
        char *const argv[] = {(char *)EDIT_BIN, (char *)"--help", NULL};
        pid_t pid = spawn(follower, argv);
        CHECK(pid > 0);
        close(follower);
        capture_len = 0;
        CHECK(read_until(leader, "Usage: edit", 5000));
        int status = 0;
        CHECK(wait_exit(pid, 5000, &status));
        CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
        close(leader);
    }

    // --version under a pty.
    {
        int leader = -1, follower = -1;
        CHECK(open_pty(&leader, &follower) == 0);
        char *const argv[] = {(char *)EDIT_BIN, (char *)"--version", NULL};
        pid_t pid = spawn(follower, argv);
        CHECK(pid > 0);
        close(follower);
        capture_len = 0;
        CHECK(read_until(leader, "edit version 1.0.0", 5000));
        int status = 0;
        CHECK(wait_exit(pid, 5000, &status));
        CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
        close(leader);
    }

    // Interactive: answer setup queries, see the UI, quit cleanly.
    {
        int leader = -1, follower = -1;
        CHECK(open_pty(&leader, &follower) == 0);
        char *const argv[] = {(char *)EDIT_BIN, NULL};
        pid_t pid = spawn(follower, argv);
        CHECK(pid > 0);
        close(follower);
        capture_len = 0;
        // Wait for the terminal queries, then answer DA (CSI c).
        CHECK(read_until(leader, "\x1b[c", 5000));
        full_write(leader, "\x1b[c", 3);
        // The UI renders the menubar (File with underlined F).
        CHECK(read_until(leader, "ile", 5000));
        CHECK(read_until(leader, "Untitled-1", 5000));
        // Alt-screen was entered.
        CHECK(memmem(capture, capture_len, "?1049h", 6) != NULL);
        // Type a character, then quit (no dirty prompt for clean exit
        // after we check dirtiness via the statusbar below).
        full_write(leader, "x", 1);
        CHECK(read_until(leader, "*", 5000));
        // Ctrl+Q with a dirty buffer prompts; lowercase "n" discards
        // (uppercase ASCII maps to Shift+N, matching Rust from_ascii).
        const char ctrl_q = 0x11;
        full_write(leader, &ctrl_q, 1);
        CHECK(read_until(leader, "Unsaved", 5000));
        full_write(leader, "n", 1);
        int status = 0;
        CHECK(wait_exit(pid, 5000, &status));
        CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
        // Drain the final restore sequences.
        read_until(leader, "?1049l", 1000);
        // Alt-screen was left on exit.
        CHECK(memmem(capture, capture_len, "?1049l", 6) != NULL);
        close(leader);
    }

    printf("test_editbin: %d checks passed\n", checks);
    return 0;
}
