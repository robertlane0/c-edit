#!/usr/bin/env python3
"""PTY differential harness: Rust edit vs C edit (build/edit).

Drives both binaries with identical keystrokes and compares emitted VT bytes.
Usage: tools/diff_edit_pty.py [scenario ...]
Scenarios: help, version, startup, type, newline, dirty-save, quit, menus.
"""
import os
import pty
import re
import select
import struct
import subprocess
import sys
import termios
import time
import fcntl
import unicodedata

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
C_BIN = os.path.join(ROOT, "build", "edit")
RUST_BIN = os.path.join(ROOT, "target", "debug", "edit")

# Scenarios whose input is split across several PTY reads: the number of
# intermediate frames then depends on process scheduling, so we compare the
# resulting terminal screen instead of the raw byte stream.
SCREEN_COMPARE = {"big-paste", "many-lines", "rapid-keys"}


def wcwidth(ch):
    o = ord(ch)
    if o == 0:
        return 0
    if unicodedata.combining(ch):
        return 0
    for lo, hi in (
        (0x1100, 0x115F),
        (0x2E80, 0xA4CF),
        (0xAC00, 0xD7A3),
        (0xF900, 0xFAFF),
        (0xFE10, 0xFE19),
        (0xFE30, 0xFE6F),
        (0xFF00, 0xFF60),
        (0xFFE0, 0xFFE6),
        (0x1F300, 0x1F64F),
        (0x1F900, 0x1F9FF),
        (0x20000, 0x3FFFD),
    ):
        if lo <= o <= hi:
            return 2
    return 1


class Screen:
    """Minimal ANSI screen emulator (CUP/ED/EL/SGR/cursor moves/save-restore)."""

    def __init__(self, cols=80, rows=24):
        self.cols, self.rows = cols, rows
        self.clear()
        self.cx = self.cy = 0
        self.saved = (0, 0)
        self.fg = self.bg = None
        self.attrs = 0

    def clear(self):
        self.grid = [[" "] * self.cols for _ in range(self.rows)]
        self.fggrid = [[None] * self.cols for _ in range(self.rows)]
        self.bggrid = [[None] * self.cols for _ in range(self.rows)]
        self.atgrid = [[0] * self.cols for _ in range(self.rows)]

    def put(self, ch):
        w = wcwidth(ch)
        if w == 0:
            return
        x = self.cx
        if x >= self.cols:
            x = self.cols - 1
        if 0 <= self.cy < self.rows and 0 <= x < self.cols:
            self.grid[self.cy][x] = ch
            self.fggrid[self.cy][x] = self.fg
            self.bggrid[self.cy][x] = self.bg
            self.atgrid[self.cy][x] = self.attrs
            if w == 2 and x + 1 < self.cols:
                self.grid[self.cy][x + 1] = ""
                self.fggrid[self.cy][x + 1] = self.fg
                self.bggrid[self.cy][x + 1] = self.bg
        self.cx += w

    def sgr(self, params):
        i = 0
        if not params:
            params = [0]
        while i < len(params):
            p = params[i]
            if p == 0:
                self.fg = self.bg = None
                self.attrs = 0
            elif p == 4:
                self.attrs |= 1
            elif p == 24:
                self.attrs &= ~1
            elif p == 7:
                self.attrs |= 2
            elif p == 27:
                self.attrs &= ~2
            elif p in (38, 48) and i + 4 < len(params) and params[i + 1] == 2:
                color = (params[i + 2], params[i + 3], params[i + 4])
                if p == 38:
                    self.fg = color
                else:
                    self.bg = color
                i += 4
            elif p == 39:
                self.fg = None
            elif p == 49:
                self.bg = None
            i += 1

    def feed(self, data):
        i, n = 0, len(data)
        while i < n:
            c = data[i]
            if c == "\x1b":
                if i + 1 < n and data[i + 1] == b"["[0]:
                    m = re.match(rb"\x1b\[([0-9;?]*)([@-~])", data[i:])
                    if not m:
                        break
                    raw, final = m.group(1), m.group(2)
                    i += m.end()
                    if raw.startswith(b"?"):
                        continue
                    params = [int(x) for x in raw.split(b";") if x != b""] if raw else []
                    if final == b"H":
                        self.cy = (params[0] - 1) if params else 0
                        self.cx = (params[1] - 1) if len(params) > 1 else 0
                        self.cy = max(0, min(self.rows - 1, self.cy))
                        self.cx = max(0, min(self.cols - 1, self.cx))
                    elif final == b"m":
                        self.sgr(params)
                    elif final == b"J":
                        mode = params[0] if params else 0
                        if mode == 2:
                            self.clear()
                            self.cx = self.cy = 0
                        elif mode == 0:
                            for x in range(self.cx, self.cols):
                                self.grid[self.cy][x] = " "
                            for y in range(self.cy + 1, self.rows):
                                self.grid[y] = [" "] * self.cols
                    elif final == b"K":
                        for x in range(self.cx, self.cols):
                            self.grid[self.cy][x] = " "
                    elif final == b"A":
                        self.cy = max(0, self.cy - (params[0] if params else 1))
                    elif final == b"B":
                        self.cy = min(self.rows - 1, self.cy + (params[0] if params else 1))
                    elif final == b"C":
                        self.cx = min(self.cols - 1, self.cx + (params[0] if params else 1))
                    elif final == b"D":
                        self.cx = max(0, self.cx - (params[0] if params else 1))
                    continue
                if i + 1 < n and data[i + 1 : i + 2] == b"]":
                    m = re.match(rb"\x1b\].*?(\x07|\x1b\\)", data[i:], re.S)
                    if not m:
                        break
                    i += m.end()
                    continue
                if i + 1 < n and data[i + 1 : i + 2] in (b"7", b"8"):
                    if data[i + 1] == 0x37:
                        self.saved = (self.cx, self.cy)
                    else:
                        self.cx, self.cy = self.saved
                    i += 2
                    continue
                if i + 1 < n and data[i + 1 : i + 2] == b"(":
                    i += 3
                    continue
                i += 2
                continue
            if c == 0x0D:
                self.cx = 0
                i += 1
                continue
            if c == 0x0A:
                self.cy = min(self.rows - 1, self.cy + 1)
                i += 1
                continue
            if c < 0x20:
                i += 1
                continue
            # Decode one UTF-8 char.
            if c < 0x80:
                nbytes = 1
            elif c >= 0xF0:
                nbytes = 4
            elif c >= 0xE0:
                nbytes = 3
            elif c >= 0xC0:
                nbytes = 2
            else:
                nbytes = 1
            chunk = data[i : i + nbytes]
            try:
                ch = chunk.decode("utf-8", "replace")
            except Exception:
                ch = "?"
            self.put(ch)
            i += nbytes

    def render(self):
        rows = []
        for y in range(self.rows):
            row = "".join(c if c else "" for c in self.grid[y])
            fgs = "".join(
                "%s|" % (",".join(map(str, self.fggrid[y][x])) if self.fggrid[y][x] else "-")
                for x in range(self.cols)
            )
            bgs = "".join(
                "%s|" % (",".join(map(str, self.bggrid[y][x])) if self.bggrid[y][x] else "-")
                for x in range(self.cols)
            )
            rows.append(row + "\x01" + fgs + "\x02" + bgs + "\x03" + "".join(map(str, self.atgrid[y])))
        return "\n".join(rows)

CTRL_Q = b"\x11"
CTRL_N = b"\x0e"
CTRL_O = b"\x0f"
CTRL_P = b"\x10"
CTRL_F = b"\x06"
CTRL_R = b"\x12"
CTRL_V = b"\x16"
CTRL_E = b"\x05"
ALT_F = b"\x1bf"  # Alt+F as ESC-prefixed key
ALT_H = b"\x1bh"  # Alt+H
RETURN = b"\r"
TAB = b"\t"


class Session:
    def __init__(self, argv, cols=80, rows=24):
        self.master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
        env = dict(os.environ)
        env["TERM"] = "xterm-256color"
        env.pop("LANG", None)
        env.pop("LC_ALL", None)
        env.pop("LANGUAGE", None)
        self.proc = subprocess.Popen(
            argv, stdin=slave, stdout=slave, stderr=slave, env=env, close_fds=True
        )
        os.close(slave)
        self.buf = bytearray()

    def read_until(self, needle, timeout=8.0):
        end = time.time() + timeout
        while time.time() < end:
            idx = self.buf.find(needle)
            if idx >= 0:
                # keep everything (frames are compared wholesale later)
                return True
            r, _, _ = select.select([self.master], [], [], 0.05)
            if r:
                try:
                    chunk = os.read(self.master, 65536)
                except OSError:
                    break
                if not chunk:
                    break
                self.buf.extend(chunk)
        return False

    def read_all(self, seconds=0.6):
        end = time.time() + seconds
        while time.time() < end:
            r, _, _ = select.select([self.master], [], [], 0.05)
            if r:
                try:
                    chunk = os.read(self.master, 65536)
                except OSError:
                    break
                if not chunk:
                    break
                self.buf.extend(chunk)

    def read_until_quiet(self, idle=0.7, limit=15.0):
        """Drains until the editor stops emitting for `idle` seconds."""
        end = time.time() + limit
        last = time.time()
        while time.time() < end:
            r, _, _ = select.select([self.master], [], [], 0.05)
            if r:
                try:
                    chunk = os.read(self.master, 65536)
                except OSError:
                    return
                if not chunk:
                    return
                self.buf.extend(chunk)
                last = time.time()
            elif time.time() - last >= idle:
                return

    def send(self, data):
        os.write(self.master, data)

    def finish(self, timeout=6.0):
        end = time.time() + timeout
        while time.time() < end:
            self.read_all(0.1)
            rc = self.proc.poll()
            if rc is not None:
                self.read_all(0.2)
                return rc
        self.proc.kill()
        self.proc.wait()
        return None

    def close(self):
        try:
            os.close(self.master)
        except OSError:
            pass


def run_scenario(binary, script, cols=80, rows=24):
    s = Session([binary], cols=cols, rows=rows)
    if not s.read_until(b"\x1b[c", 8.0):
        # help/version scenarios never query the terminal
        pass
    else:
        s.send(b"\x1b[c")
        # Let the editor finish the setup handshake before sending input,
        # otherwise early keystrokes are consumed by the setup parser.
        s.read_until_quiet(0.4, 4.0)
    for step in script:
        # Large payloads are chunked so both editors consume the whole write
        # before the next render; otherwise PTY buffering makes runs racy.
        if len(step) > 4096:
            for i in range(0, len(step), 4096):
                s.send(step[i : i + 4096])
                s.read_until_quiet(0.2)
        else:
            s.send(step)
            s.read_until_quiet()
    s.read_until_quiet()
    rc = s.finish()
    s.close()
    return bytes(s.buf), rc


CTRL_Z = b"\x1a"
BACKSPACE = b"\x7f"
# SGR mouse (1006): press/release + motion
MOUSE_DOWN = b"\x1b[<0;10;5M"
MOUSE_UP = b"\x1b[<0;10;5m"
MOUSE_DRAG = b"\x1b[<32;12;6M"
SCROLL_UP = b"\x1b[<64;10;5M"
SCROLL_DOWN = b"\x1b[<65;10;5M"


def mouse_click(x, y, button=0, release=True):
    seq = b"\x1b[<%d;%d;%dM" % (button, x, y)
    if release:
        seq += b"\x1b[<%d;%d;%dm" % (button, x, y)
    return seq


SCENARIOS = {
    "startup": [],
    "type": [b"hello"],
    "newline": [b"hello", b"\r", b"world"],
    "menus": [ALT_F],
    "menu-file": [ALT_F, b"o"],
    "menu-edit": [CTRL_E],
    "menu-view": [CTRL_V, b"w"],  # Ctrl+V, Alt+W toggle word wrap
    "search": [CTRL_F, b"hello", b"\r"],
    "replace": [CTRL_R, b"hello", b"\r", b"hi", b"\r"],
    "dirty-save": [b"abc", CTRL_Q, b"s"],
    "quit": [CTRL_Q, b"n"],
    "new-doc": [CTRL_N],
    "close-doc": [CTRL_Q, b"n", CTRL_Q],
    "unsaved-prompt": [b"zz", CTRL_Q, b"n"],
    "doc-picker": [CTRL_N, CTRL_P, b"\r"],
    "about": [ALT_H, b"a", b"\r"],  # Alt+H, About, OK
    "utf8": ["héllo→".encode()],
    "paste": [b"\x1b[200~pasted\x1b[201~"],
    "arrows": [b"abc", b"\x1b[D", b"\x1b[D", b"X"],
    "home-end": [b"abc\rdef", b"\x1b[H", b"Z", b"\x1b[F", b"Y"],
    "delete": [b"abc", b"\x1b[D", BACKSPACE],
    "enter-multi": [b"a", b"\r", b"b", b"\r", b"c", b"\r", b"d"],
    "backspace": [b"abc", BACKSPACE, BACKSPACE],
    "tab-indent": [b"a", b"\r", TAB, b"b"],
    "ctrl-z": [b"abc", CTRL_Z, b"x"],
    "shift-tab": [b"a", b"\r", b"\t", b"\x1b[Z", b"b"],
    "file-picker-open": [CTRL_O, b"*.rs\r", b"\r"],
    "file-picker-cancel": [CTRL_O, b"\x1b"],
    "encoding-picker": [mouse_click(1, 23, 0, False), b"\x1b[<0m"],
    "indent-picker": [mouse_click(30, 23), b"\x1b[B"],
    "mouse-select": [MOUSE_DOWN, MOUSE_UP, b"X"],
    "mouse-drag-select": [MOUSE_DOWN, MOUSE_DRAG, b"\x1b[<32;12;6m", b"\x1b[C", b"Q"],
    "scroll-wheel": [b"a\r" * 30, SCROLL_UP, SCROLL_UP, SCROLL_DOWN],
    "resize": [b"abc", b"\x1b[8;30;100t", b"de"],
    "long-line": [b"x" * 500, b"\x1b[F", b"y" * 100, b"\x1b[H", b"z"],
    "many-lines": [b"".join(b"line%d\r" % i for i in range(120))],
    "rapid-keys": [bytes(range(0x61, 0x7B))],  # a-z in one write
    "big-paste": [b"\x1b[200~" + b"p" * 20000 + b"\x1b[201~"],
    "wide-glyphs": ["日本語テキスト\r".encode() * 5, b"\x1b[F", "→←".encode()],
    "search-no-match": [CTRL_F, b"zzzz", b"\r", b"\x1b"],
    "search-regex": [CTRL_F, b"a+", b"\r", b"\x1b"],
    "wrap-toggle-nav": [b"a\r" * 5, CTRL_V, b"w", b"\x1b[F", b"z"],
    "many-docs": [CTRL_N, CTRL_N, CTRL_N, CTRL_P, b"\x1b[B", b"\r"],
    "undo-redo": [b"abc", CTRL_Z, CTRL_Z, b"x", b"\x1a", b"\r"],
    "cut-copy-paste": [b"abc", b"\x01", b"\x16", b"\x1a", b"\x1b", b"\x16"],
}


def normalize(data):
    # Everything after the alt-screen restore is terminal echo/teardown noise
    # whose timing depends on process exit scheduling.
    marker = data.rfind(b"\x1b[?1049l")
    if marker >= 0:
        return data[: marker + len(b"\x1b[?1049l")]
    return data


def main():
    names = sys.argv[1:] or list(SCENARIOS)
    failures = 0
    for name in names:
        if name not in SCENARIOS:
            print(f"unknown scenario {name}")
            failures += 1
            continue
        script = SCENARIOS[name]
        rust_out, rust_rc = run_scenario(RUST_BIN, script)
        c_out, c_rc = run_scenario(C_BIN, script)
        if name in SCREEN_COMPARE:
            rs, cs = Screen(), Screen()
            rs.feed(rust_out)
            cs.feed(c_out)
            same = rs.render() == cs.render()
            if not same:
                rl, cl = rs.render().split("\n"), cs.render().split("\n")
                for idx, (a, b) in enumerate(zip(rl, cl)):
                    if a != b:
                        print(f"  screen row {idx} differs")
                        print("   rust:", repr(a[:120]))
                        print("   c   :", repr(b[:120]))
                        break
            print(
                f"[{'OK  ' if same else 'DIFF'}] {name} (screen): rust={len(rust_out)}B c={len(c_out)}B"
            )
        else:
            rust_out, c_out = normalize(rust_out), normalize(c_out)
            same = rust_out == c_out
        status = "OK  " if same and rust_rc == c_rc else "DIFF"
        if not (same and rust_rc == c_rc):
            failures += 1
            print(f"[{status}] {name}: rust={len(rust_out)}B rc={rust_rc} c={len(c_out)}B rc={c_rc}")
        else:
            print(f"[{status}] {name}: rust={len(rust_out)}B rc={rust_rc} c={len(c_out)}B rc={c_rc}")
        if not same and name not in SCREEN_COMPARE:
            # show first difference
            n = min(len(rust_out), len(c_out))
            i = 0
            while i < n and rust_out[i] == c_out[i]:
                i += 1
            lo = max(0, i - 60)
            print("  rust: " + repr(rust_out[lo : i + 90]))
            print("  c   : " + repr(c_out[lo : i + 90]))
        if rust_rc != c_rc:
            print(f"  exit differs: rust={rust_rc} c={c_rc}")
    print("scenarios failed:", failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
