// The `edit` editor in safe C (cf. src/bin/edit/main.rs).
//
// Frame loop: read stdin -> VT parse -> input parse -> app draw ->
// settle -> render -> write stdout. Errors print like Rust and exit 1.

#define _DEFAULT_SOURCE // strdup; must precede all headers

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "edit/app.h"
#include "edit/apploc.h"
#include "edit/arena.h"
#include "edit/base64.h"
#include "edit/fb.h"
#include "edit/helpers.h"
#include "edit/input.h"
#include "edit/tty.h"
#include "edit/tui.h"
#include "edit/vt.h"

// 512MiB on 64-bit like Rust SCRATCH_ARENA_CAPACITY.
#if UINTPTR_MAX > 0xFFFFFFFFU
#define SCRATCH_CAP ((size_t)512 * 1024 * 1024)
#else
#define SCRATCH_CAP ((size_t)128 * 1024 * 1024)
#endif

#define CLIP_THRESHOLD ((size_t)4 * 1024)

static void write_str(const char *s) {
    if (s != NULL && s[0] != '\0') {
        edit_tty_write((const uint8_t *)s, strlen(s));
    }
}

static void setup_terminal(edit_tui_t *tui, edit_vt_parser_t *vt) {
    write_str("\x1b[?1049h\x1b[?1002;1006;2004h"
              "\x1b]4;0;?;1;?;2;?;3;?;4;?;5;?;6;?;7;?\x07"
              "\x1b]4;8;?;9;?;10;?;11;?;12;?;13;?;14;?;15;?\x07"
              "\x1b]10;?\x07\x1b]11;?\x07"
              "\x1b[c");

    uint32_t theme[EDIT_FB_COLORS];
    for (int i = 0; i < EDIT_FB_COLORS; ++i) {
        theme[i] = EDIT_FB_DEFAULT_THEME[i];
    }
    int responses = 0;
    bool done = false;
    // Accumulated OSC payload across partial tokens.
    char *osc = NULL;
    size_t osc_len = 0;
    size_t osc_cap = 0;

    while (!done) {
        uint8_t *input = NULL;
        size_t input_len = 0;
        uint64_t timeout = edit_vt_timeout_ms(vt);
        int64_t timeout_ms = timeout > (uint64_t)INT64_MAX ? EDIT_TUI_FOREVER : (int64_t)timeout;
        if (edit_tty_read(&input, &input_len, timeout_ms) != EDIT_TTY_DATA) {
            free(input);
            break;
        }
        edit_vt_stream_t stream;
        edit_vt_parse(vt, input, input_len, &stream);
        edit_token_t tok;
        while (edit_vt_next(&stream, &tok)) {
            if (tok.kind == EDIT_VT_CSI && tok.csi.final_byte == (uint32_t)'c') {
                done = true;
                break;
            }
            if (tok.kind != EDIT_VT_OSC) {
                continue;
            }
            const uint8_t *data = tok.ptr;
            size_t data_len = tok.len;
            if (osc_len > 0) {
                // Continued payload: append.
                if (data_len > 0) {
                    if (osc_len + data_len + 1 > osc_cap) {
                        size_t grown = osc_len + data_len + 1;
                        char *nb = (char *)realloc(osc, grown);
                        if (nb == NULL) {
                            break;
                        }
                        osc = nb;
                        osc_cap = grown;
                    }
                    memcpy(osc + osc_len, data, data_len);
                    osc_len += data_len;
                }
                if (tok.partial) {
                    continue;
                }
                data = (const uint8_t *)osc;
                data_len = osc_len;
            } else if (tok.partial) {
                if (data_len > 0) {
                    if (data_len + 1 > osc_cap) {
                        char *nb = (char *)realloc(osc, data_len + 1);
                        if (nb == NULL) {
                            break;
                        }
                        osc = nb;
                        osc_cap = data_len + 1;
                    }
                    memcpy(osc, data, data_len);
                    osc_len = data_len;
                }
                continue;
            }
            // data = "4;<idx>;rgb:r/g/b" | "10;..." | "11;...".
            char *buf = (char *)malloc(data_len + 1);
            if (buf == NULL) {
                break;
            }
            memcpy(buf, data, data_len);
            buf[data_len] = '\0';
            osc_len = 0;
            uint32_t *color = NULL;
            char *save = NULL;
            char *head = strtok_r(buf, ";", &save);
            if (head != NULL && strcmp(head, "4") == 0) {
                char *idxs = strtok_r(NULL, ";", &save);
                if (idxs != NULL) {
                    char *end = NULL;
                    long idx = strtol(idxs, &end, 10);
                    if (end != idxs && *end == '\0' && idx >= 0 && idx < 16) {
                        color = &theme[idx];
                    }
                }
            } else if (head != NULL && strcmp(head, "10") == 0) {
                color = &theme[EDIT_FB_FOREGROUND];
            } else if (head != NULL && strcmp(head, "11") == 0) {
                color = &theme[EDIT_FB_BACKGROUND];
            }
            if (color != NULL) {
                char *param = strtok_r(NULL, ";", &save);
                if (param != NULL && strncmp(param, "rgb:", 4) == 0) {
                    uint32_t rgb = 0;
                    bool ok = true;
                    char *comp = param + 4;
                    for (int c = 0; c < 3; ++c) {
                        char *slash = (c < 2) ? strchr(comp, '/') : NULL;
                        size_t clen = slash != NULL ? (size_t)(slash - comp) : strlen(comp);
                        if (clen != 2 && clen != 4) {
                            // Rust skips components of other lengths.
                            if (slash != NULL) {
                                comp = slash + 1;
                            }
                            continue;
                        }
                        unsigned val = 0;
                        for (size_t k = 0; k < clen; ++k) {
                            char ch = comp[k];
                            unsigned d = 0;
                            if (ch >= '0' && ch <= '9') {
                                d = (unsigned)(ch - '0');
                            } else if (ch >= 'a' && ch <= 'f') {
                                d = (unsigned)(ch - 'a' + 10);
                            } else if (ch >= 'A' && ch <= 'F') {
                                d = (unsigned)(ch - 'A' + 10);
                            } else {
                                ok = false;
                                break;
                            }
                            val = val * 16 + d;
                        }
                        if (!ok) {
                            break;
                        }
                        if (clen == 4) {
                            val = (val * 0xFFU + 0x7FFFU) / 0xFFFFU;
                        }
                        rgb = (rgb >> 8) | (val << 16);
                        if (slash != NULL) {
                            comp = slash + 1;
                        }
                    }
                    if (ok) {
                        *color = rgb | 0xFF000000U;
                        responses += 1;
                    }
                }
            }
            free(buf);
        }
        free(input);
    }
    free(osc);

    if (responses == EDIT_FB_COLORS) {
        edit_fb_set_theme(&tui->framebuffer, theme);
    }
}

static void restore_terminal(void) {
    // Cursor style reset + show cursor + title reset + mouse/alt-screen off.
    write_str("\x1b[0 q\x1b[?25h\x1b]0;\x07\x1b[?1002;1006;2004l\x1b[?1049l");
}

static int run(int argc, char **argv) {
    edit_error_t err = edit_error_app(0);
    if (edit_tty_init(&err) != 0) {
        char *msg = edit_app_error_text(err);
        if (msg != NULL) {
            edit_tty_write((const uint8_t *)msg, strlen(msg));
            edit_tty_write((const uint8_t *)"\r\n", 2);
            free(msg);
        }
        return 1;
    }
    if (edit_scratch_init(SCRATCH_CAP) != 0) {
        edit_tty_deinit();
        return 1;
    }
    edit_loc_init();

    int rc = 1;
    edit_app_t app;
    if (edit_app_init(&app) != 0) {
        goto done_scratch;
    }
    bool early = false;
    if (edit_app_handle_args(&app, argc, argv, &early, &err) != 0) {
        char *msg = edit_app_error_text(err);
        if (msg != NULL) {
            write_str(msg);
            write_str("\r\n");
            free(msg);
        }
        goto done_app;
    }
    if (early) {
        rc = 0;
        goto done_app;
    }
    if (edit_tty_switch_modes(&err) != 0) {
        char *msg = edit_app_error_text(err);
        if (msg != NULL) {
            write_str(msg);
            write_str("\r\n");
            free(msg);
        }
        goto done_app;
    }

    edit_vt_parser_t vt_parser;
    edit_vt_init(&vt_parser);
    edit_in_parser_t input_parser;
    edit_in_init(&input_parser);
    edit_tui_t tui;
    if (edit_tui_init(&tui) != 0) {
        goto done_app;
    }
    setup_terminal(&tui, &vt_parser);
    edit_app_setup_colors(&app, &tui);
    edit_tui_set_modifiers(&tui, edit_loc(EDIT_LOC_CTRL), edit_loc(EDIT_LOC_ALT),
                           edit_loc(EDIT_LOC_SHIFT));
    edit_tty_inject_resize();

    for (;;) {
        // Process a batch of input.
        {
            uint64_t vt_timeout = edit_vt_timeout_ms(&vt_parser);
            int64_t tui_timeout = edit_tui_read_timeout(&tui);
            int64_t timeout = tui_timeout;
            if (vt_timeout != UINT64_MAX &&
                (timeout == EDIT_TUI_FOREVER || (uint64_t)timeout > vt_timeout)) {
                timeout =
                    (vt_timeout > (uint64_t)INT64_MAX) ? EDIT_TUI_FOREVER : (int64_t)vt_timeout;
            }
            uint8_t *input = NULL;
            size_t input_len = 0;
            if (edit_tty_read(&input, &input_len, timeout) != EDIT_TTY_DATA) {
                free(input);
                break;
            }
            edit_vt_stream_t vt_stream;
            edit_vt_parse(&vt_parser, input, input_len, &vt_stream);
            edit_in_stream_t in_stream;
            edit_in_parse(&input_parser, &vt_stream, &in_stream);
            for (;;) {
                edit_input_t event;
                memset(&event, 0, sizeof event);
                if (!edit_in_next(&in_stream, &event)) {
                    break;
                }
                edit_ctx_t ctx;
                if (edit_tui_begin(&tui, &event, &ctx) != 0) {
                    continue;
                }
                edit_app_draw(&ctx, &app);
                edit_tui_end(&tui, &ctx);
            }
            free(input);
        }

        // Continue rendering until the layout has settled.
        while (edit_tui_needs_settling(&tui)) {
            edit_ctx_t ctx;
            if (edit_tui_begin(&tui, NULL, &ctx) != 0) {
                break;
            }
            edit_app_draw(&ctx, &app);
            edit_tui_end(&tui, &ctx);
        }

        if (app.exit) {
            break;
        }
        // Render the UI and write it to the terminal.
        {
            const char *output = NULL;
            size_t output_len = edit_tui_render(&tui, &output);
            if (output != NULL && output_len > 0) {
                edit_tty_write((const uint8_t *)output, output_len);
            }
            // Title is appended after the frame output (cf. write_terminal_title).
            edit_app_doc_t *doc = edit_app_active(&app);
            const char *filename = (doc != NULL && doc->filename != NULL) ? doc->filename : "";
            if (app.osc_title_filename == NULL || strcmp(filename, app.osc_title_filename) != 0) {
                size_t n = edit_app_title_seq(NULL, 0, filename);
                char *seq = (char *)malloc(n + 1);
                if (seq != NULL) {
                    edit_app_title_seq(seq, n + 1, filename);
                    edit_tty_write((const uint8_t *)seq, n);
                    free(seq);
                }
                free(app.osc_title_filename);
                app.osc_title_filename = strdup(filename);
            }

            if (app.osc_clip_send == edit_tui_clipboard_gen(&tui)) {
                size_t clip_len = 0;
                const uint8_t *clip = edit_tui_clipboard(&tui, &clip_len);
                if (clip != NULL && clip_len > 0) {
                    size_t n = edit_app_clipboard_seq(NULL, 0, clip, clip_len);
                    char *seq = (char *)malloc(n + 1);
                    if (seq != NULL) {
                        edit_app_clipboard_seq(seq, n + 1, clip, clip_len);
                        edit_tty_write((const uint8_t *)seq, n);
                        free(seq);
                    }
                }
                uint32_t gen = edit_tui_clipboard_gen(&tui);
                app.osc_clip_send = gen - 1;
            }
        }
    }

    rc = 0;
    edit_tui_destroy(&tui);
    restore_terminal();
done_app:
    edit_app_destroy(&app);
done_scratch:
    edit_scratch_destroy();
    edit_tty_deinit();
    return rc;
}

int main(int argc, char **argv) {
    return run(argc, argv);
}
