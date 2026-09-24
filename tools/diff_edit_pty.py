#!/usr/bin/env python3
"""PTY differential harness: Rust edit vs C edit (build/edit).

Drives both binaries with identical keystrokes and compares emitted VT bytes.
Usage: tools/diff_edit_pty.py [scenario ...]
Scenarios: help, version, startup, type, newline, dirty-save, quit, menus.
"""
import os
import pty
import select
import signal
import subprocess
import sys
import termios
import time
import fcntl
import struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
C_BIN = os.path.join(ROOT, "build", "edit")
RUST_BIN = os.path.join(ROOT, "target", "debug", "edit")

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
    for step in script:
        s.send(step)
        s.read_all(0.35)
    s.read_all(0.5)
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
        rust_out, c_out = normalize(rust_out), normalize(c_out)
        same = rust_out == c_out
        status = "OK  " if same and rust_rc == c_rc else "DIFF"
        if not (same and rust_rc == c_rc):
            failures += 1
        print(f"[{status}] {name}: rust={len(rust_out)}B rc={rust_rc} c={len(c_out)}B rc={c_rc}")
        if not same:
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
