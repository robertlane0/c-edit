#define _POSIX_C_SOURCE 200809L // ppoll, nanosleep; must precede headers
#define _GNU_SOURCE // ppoll declaration

#include "edit/tty.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "edit/utf8.h"

#define TTY_BUF ((size_t)4 * 1024)

static struct {
    int stdin_fd;
    int stdout_fd;
    int stdin_flags;
    struct termios saved_termios;
    bool has_termios;
    bool inject_resize;
    uint8_t utf8_buf[4];
    size_t utf8_len;
} s_state = {STDIN_FILENO, STDOUT_FILENO, 0, {0}, false, false, {0}, 0};

static void sigwinch_handler(int sig) {
    (void)sig;
    s_state.inject_resize = true;
}

int edit_tty_init(edit_error_t *err) {
    if (!isatty(s_state.stdin_fd)) {
        int fd = open("/dev/tty", O_RDONLY);
        if (fd < 0) {
            if (err != NULL) {
                *err = edit_error_sys((uint32_t)errno);
            }
            return -1;
        }
        s_state.stdin_fd = fd;
    }
    int flags = fcntl(s_state.stdin_fd, F_GETFL);
    if (flags < 0) {
        if (err != NULL) {
            *err = edit_error_sys((uint32_t)errno);
        }
        return -1;
    }
    s_state.stdin_flags = flags;
    return 0;
}

void edit_tty_deinit(void) {
    if (s_state.has_termios) {
        s_state.has_termios = false;
        tcsetattr(s_state.stdout_fd, TCSANOW, &s_state.saved_termios);
    }
}

int edit_tty_switch_modes(edit_error_t *err) {
    struct sigaction action;
    memset(&action, 0, sizeof action);
    action.sa_handler = sigwinch_handler;
    if (sigaction(SIGWINCH, &action, NULL) != 0) {
        if (err != NULL) {
            *err = edit_error_sys((uint32_t)errno);
        }
        return -1;
    }
    struct termios tio;
    if (tcgetattr(s_state.stdin_fd, &tio) != 0) {
        if (err != NULL) {
            *err = edit_error_sys((uint32_t)errno);
        }
        return -1;
    }
    s_state.saved_termios = tio;
    s_state.has_termios = true;

    tio.c_iflag &=
        (unsigned)(~(IGNBRK | BRKINT | PARMRK | INPCK | ISTRIP | INLCR | IGNCR | ICRNL | IXON));
    tio.c_oflag &= (unsigned)(~OPOST);
    tio.c_cflag &= (unsigned)(~(CSIZE | PARENB));
    tio.c_cflag |= (unsigned)CS8;
    tio.c_lflag &= (unsigned)(~(ISIG | ICANON | ECHO | ECHONL | IEXTEN));
    tio.c_lflag &= (unsigned)(~(ICANON | ECHO));
    if (tcsetattr(s_state.stdin_fd, TCSANOW, &tio) != 0) {
        if (err != NULL) {
            *err = edit_error_sys((uint32_t)errno);
        }
        return -1;
    }
    return 0;
}

void edit_tty_bind_fds(int stdin_fd, int stdout_fd) {
    s_state.stdin_fd = stdin_fd;
    s_state.stdout_fd = stdout_fd;
    int flags = fcntl(stdin_fd, F_GETFL);
    s_state.stdin_flags = flags < 0 ? 0 : flags;
    s_state.has_termios = false;
    s_state.inject_resize = false;
    s_state.utf8_len = 0;
}

void edit_tty_inject_resize(void) {
    s_state.inject_resize = true;
}

static void set_nonblocking(bool nonblock) {
    bool is_nonblock = (s_state.stdin_flags & O_NONBLOCK) != 0;
    if (is_nonblock != nonblock) {
        s_state.stdin_flags ^= O_NONBLOCK;
        (void)fcntl(s_state.stdin_fd, F_SETFL, s_state.stdin_flags);
    }
}

static int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void window_size(uint16_t *out_w, uint16_t *out_h) {
    struct winsize ws;
    memset(&ws, 0, sizeof ws);
    for (int attempt = 1;; ++attempt) {
        if (ioctl(s_state.stdout_fd, TIOCGWINSZ, &ws) != 0 || (ws.ws_col != 0 && ws.ws_row != 0)) {
            break;
        }
        if (attempt == 10) {
            ws.ws_col = 80;
            ws.ws_row = 24;
            break;
        }
        struct timespec sleep = {0, (long)attempt * 10 * 1000 * 1000};
        nanosleep(&sleep, NULL);
    }
    *out_w = ws.ws_col;
    *out_h = ws.ws_row;
}

static bool buf_append(uint8_t **buf, size_t *len, size_t *cap, const uint8_t *src, size_t n) {
    if (n == 0) {
        return true;
    }
    if (*len > (size_t)-1 - n) {
        return false;
    }
    if (*len + n > *cap) {
        size_t grown = *cap != 0 ? *cap : 256;
        while (grown < *len + n) {
            if (grown > (size_t)-1 / 2) {
                grown = *len + n;
                break;
            }
            grown *= 2;
        }
        uint8_t *nb = (uint8_t *)realloc(*buf, grown);
        if (nb == NULL) {
            return false;
        }
        *buf = nb;
        *cap = grown;
    }
    memcpy(*buf + *len, src, n);
    *len += n;
    return true;
}

edit_tty_read_t edit_tty_read(uint8_t **out, size_t *out_len, int64_t timeout_ms) {
    if (out != NULL) {
        *out = NULL;
    }
    if (out_len != NULL) {
        *out_len = 0;
    }
    if (out == NULL || out_len == NULL) {
        return EDIT_TTY_CLOSED;
    }
    if (s_state.inject_resize) {
        timeout_ms = 0;
    }
    bool poll = timeout_ms != EDIT_TTY_WAIT_FOREVER;
    uint8_t *buf = NULL;
    size_t len = 0;
    size_t cap = 0;
    if (!buf_append(&buf, &len, &cap, s_state.utf8_buf, s_state.utf8_len)) {
        return EDIT_TTY_CLOSED;
    }
    s_state.utf8_len = 0;

    for (;;) {
        if (timeout_ms != EDIT_TTY_WAIT_FOREVER) {
            int64_t beg = now_ms();
            struct pollfd pfd;
            pfd.fd = s_state.stdin_fd;
            pfd.events = POLLIN;
            pfd.revents = 0;
            struct timespec ts;
            int64_t ms = timeout_ms < 0 ? 0 : timeout_ms;
            ts.tv_sec = (time_t)(ms / 1000);
            ts.tv_nsec = (long)(ms % 1000) * 1000 * 1000;
            int ret = ppoll(&pfd, 1, &ts, NULL);
            if (ret < 0) {
                free(buf);
                return EDIT_TTY_CLOSED;
            }
            if (ret == 0) {
                break;
            }
            int64_t spent = now_ms() - beg;
            timeout_ms -= spent < 0 ? 0 : spent;
            if (timeout_ms < 0) {
                timeout_ms = 0;
            }
        }
        set_nonblocking(poll);
        // Read into a stack window, then append (keeps borrowing simple).
        uint8_t window[TTY_BUF];
        ssize_t n = read(s_state.stdin_fd, window, sizeof window);
        if (n > 0) {
            if (!buf_append(&buf, &len, &cap, window, (size_t)n)) {
                free(buf);
                return EDIT_TTY_CLOSED;
            }
            break;
        }
        if (n == 0) {
            free(buf);
            return EDIT_TTY_CLOSED;
        }
        int e = errno;
        if (e == EINTR && s_state.inject_resize) {
            break;
        }
        if (e == EAGAIN && timeout_ms == 0) {
            break;
        }
        if (e != EINTR && e != EAGAIN) {
            free(buf);
            return EDIT_TTY_CLOSED;
        }
    }

    if (len > 0) {
        // Stash a trailing incomplete UTF-8 sequence for the next read.
        size_t lim = len > 3 ? len - 3 : 0;
        size_t off = len - 1;
        while (off > lim && (buf[off] & 0xC0U) == 0x80U) {
            off -= 1;
        }
        uint8_t lead = buf[off];
        size_t seqlen = 0;
        if ((lead & 0x80U) == 0) {
            seqlen = 1;
        } else if ((lead & 0xE0U) == 0xC0U) {
            seqlen = 2;
        } else if ((lead & 0xF0U) == 0xE0U) {
            seqlen = 3;
        } else if ((lead & 0xF8U) == 0xF0U) {
            seqlen = 4;
        }
        if (seqlen > 0 && off + seqlen > len) {
            s_state.utf8_len = len - off;
            memcpy(s_state.utf8_buf, buf + off, s_state.utf8_len);
            len = off;
        }
    }

    // Lossy decode into a fresh heap buffer (one FFFD per error step).
    uint8_t *text = NULL;
    size_t text_len = 0;
    size_t text_cap = 0;
    {
        edit_utf8_chars_t it;
        edit_utf8_chars_init(&it, buf, len, 0);
        uint32_t cp = 0;
        char enc[4];
        while (edit_utf8_next(&it, &cp)) {
            size_t n = edit_utf8_encode(cp, enc);
            if (n == 0 || !buf_append(&text, &text_len, &text_cap, (const uint8_t *)enc, n)) {
                free(buf);
                free(text);
                return EDIT_TTY_CLOSED;
            }
        }
    }
    free(buf);

    // Prepend a synthetic size report after SIGWINCH.
    if (s_state.inject_resize) {
        s_state.inject_resize = false;
        uint16_t w = 0;
        uint16_t h = 0;
        window_size(&w, &h);
        if (w > 0 && h > 0) {
            char seq[32];
            int n = snprintf(seq, sizeof seq, "\x1b[8;%u;%ut", h, w);
            if (n > 0) {
                uint8_t *combo = (uint8_t *)malloc(text_len + (size_t)n);
                if (combo != NULL) {
                    memcpy(combo, seq, (size_t)n);
                    memcpy(combo + (size_t)n, text, text_len);
                    free(text);
                    text = combo;
                    text_len += (size_t)n;
                }
            }
        }
    }

    if (text_len == 0) {
        free(text);
        return EDIT_TTY_TIMEOUT;
    }
    *out = text;
    *out_len = text_len;
    return EDIT_TTY_DATA;
}

bool edit_tty_write(const uint8_t *text, size_t len) {
    if (len == 0) {
        return true;
    }
    if (text == NULL) {
        return false;
    }
    set_nonblocking(false);
    size_t written = 0;
    while (written < len) {
        size_t chunk = len - written;
        const size_t gig = (size_t)1000 * 1000 * 1000;
        if (chunk > gig) {
            chunk = gig;
        }
        ssize_t n = write(s_state.stdout_fd, text + written, chunk);
        if (n >= 0) {
            written += (size_t)n;
            continue;
        }
        if (errno != EINTR) {
            return false;
        }
    }
    return true;
}

bool edit_tty_stdin_redirected(void) {
    return s_state.stdin_fd != STDIN_FILENO;
}

bool edit_tty_file_id(int fd, uint64_t *out_dev, uint64_t *out_ino) {    struct stat st;
    if (fstat(fd, &st) != 0) {
        return false;
    }
    if (out_dev != NULL) {
        *out_dev = (uint64_t)st.st_dev;
    }
    if (out_ino != NULL) {
        *out_ino = (uint64_t)st.st_ino;
    }
    return true;
}

bool edit_tty_file_id_at(const char *path, uint64_t *out_dev, uint64_t *out_ino) {
    if (path == NULL) {
        return false;
    }
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    bool ok = edit_tty_file_id(fd, out_dev, out_ino);
    close(fd);
    return ok;
}

size_t edit_tty_languages(char ***out) {
    static const char *keys[] = {"LANGUAGE", "LC_ALL", "LANG"};
    if (out != NULL) {
        *out = NULL;
    }
    for (size_t k = 0; k < 3; ++k) {
        const char *val = getenv(keys[k]);
        if (val == NULL) {
            continue;
        }
        // Count non-empty ':' segments.
        size_t n = 0;
        const char *p = val;
        for (;;) {
            while (*p == ':') {
                ++p;
            }
            if (*p == '\0') {
                break;
            }
            ++n;
            while (*p != '\0' && *p != ':') {
                ++p;
            }
        }
        if (n == 0) {
            break; // present but empty: stop like Rust (breaks on first set var)
        }
        char **list = (char **)malloc((n + 1) * sizeof *list);
        if (list == NULL) {
            return 0;
        }
        size_t i = 0;
        p = val;
        for (;;) {
            while (*p == ':') {
                ++p;
            }
            if (*p == '\0') {
                break;
            }
            const char *beg = p;
            while (*p != '\0' && *p != ':') {
                ++p;
            }
            size_t m = (size_t)(p - beg);
            list[i] = (char *)malloc(m + 1);
            if (list[i] == NULL) {
                while (i > 0) {
                    free(list[--i]);
                }
                free(list);
                return 0;
            }
            memcpy(list[i], beg, m);
            list[i][m] = '\0';
            ++i;
        }
        list[i] = NULL;
        if (out != NULL) {
            *out = list;
        } else {
            for (size_t j = 0; j < i; ++j) {
                free(list[j]);
            }
            free(list);
        }
        return n;
    }
    return 0;
}

size_t edit_tty_error_text(uint32_t code, char *dst, size_t cap) {
    char num[32];
    int nn = snprintf(num, sizeof num, "Error %u", code);
    const char *msg = strerror((int)code);
    size_t n = nn > 0 ? (size_t)nn : 0;
    size_t m = (msg != NULL) ? strlen(msg) : 0;
    size_t need = n + (m > 0 ? 2 + m : 0);
    if (dst != NULL && cap > 0) {
        size_t k = 0;
        size_t q = n < cap - 1 ? n : cap - 1;
        memcpy(dst, num, q);
        k = q;
        if (m > 0 && k + 2 < cap) {
            dst[k++] = ':';
            dst[k++] = ' ';
            q = m < cap - 1 - k ? m : cap - 1 - k;
            memcpy(dst + k, msg, q);
            k += q;
        }
        dst[k] = '\0';
    }
    return need;
}

bool edit_tty_is_not_found(edit_error_t err) {
    return edit_error_equal(err, edit_error_sys((uint32_t)ENOENT));
}
