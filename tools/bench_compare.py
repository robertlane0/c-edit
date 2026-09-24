#!/usr/bin/env python3
"""Compare C benchmark output against criterion (Rust) output.

Usage:
  python3 tools/bench_compare.py [--c FILE] [--rust FILE] [--run]

With --run (or missing inputs) the C bench is built+run and criterion is run.
C lines look like:  "simd/memchr2/8 9 4.541 ..." (name size ns/op bytes/s).
Criterion lines:    "simd/simd/memchr2/8   time:   [4.1 ns 4.2 ns 4.3 ns]".
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
C_OUT = "/var/tmp/opencode/bench/c_bench.txt"
RUST_OUT = "/var/tmp/opencode/bench/rust_bench.txt"


def parse_c(path):
    out = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            parts = line.split()
            if len(parts) < 3:
                continue
            name = parts[0]
            try:
                ns = float(parts[2])
            except ValueError:
                continue
            out[name] = ns
    return out


TIME_RE = re.compile(
    r"time:\s*\[\s*(?P<lo>[0-9.]+)\s*(?P<unit>ns|\u00b5s|us|ms|s)\s+"
    r"(?P<mid>[0-9.]+)\s*(?P<unit2>ns|\u00b5s|us|ms|s)\s+"
    r"(?P<hi>[0-9.]+)\s*(?P<unit3>ns|\u00b5s|us|ms|s)"
)
SCALE = {"ns": 1.0, "us": 1e3, "\u00b5s": 1e3, "ms": 1e6, "s": 1e9}


def parse_rust(path):
    out = {}
    current = None
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = re.match(r"^Benchmarking\s+(\S+)", line)
            if m:
                # Names may contain "::"; only the trailing status colon goes.
                current = m.group(1).rstrip(":")
                continue
            m = TIME_RE.search(line)
            if not m or current is None:
                continue
            unit = m.group("unit")
            if unit != m.group("unit2") or unit != m.group("unit3"):
                continue
            out[current] = float(m.group("mid")) * SCALE[unit]
            current = None
    return out


def norm(name):
    """Normalize Rust group/bench names to the C naming scheme."""
    n = name
    n = re.sub(r"^hash/hash/", "hash/", n)
    n = re.sub(r"^oklab/oklab/", "oklab/", n)
    n = re.sub(r"^simd/simd/", "simd/", n)
    n = n.replace("unicode::MeasurementConfig::", "unicode/MeasurementConfig/")
    n = n=n.replace("unicode::Utf8Chars/next", "unicode/Utf8Chars/next")
    n=n.replace("unicode::Utf8Chars::", "unicode/Utf8Chars/")
    n = n.replace("memset<u32>", "memset<u32>")
    return n


def run_c():
    subprocess.run(["make", "clean", "all"], cwd=ROOT, check=True,
                   stdout=subprocess.DEVNULL)
    subprocess.run(["make", "build/bench_c"], cwd=ROOT, check=True,
                   stdout=subprocess.DEVNULL)
    with open(C_OUT, "w", encoding="utf-8") as f:
        subprocess.run([os.path.join(ROOT, "build", "bench_c")], stdout=f, check=True)


def run_rust():
    with open(RUST_OUT, "w", encoding="utf-8") as f:
        subprocess.run(
            ["cargo", "+nightly", "bench", "--bench", "lib", "--",
             "--warm-up-time", "1", "--measurement-time", "2", "--sample-size", "30"],
            cwd=ROOT, stdout=f, stderr=subprocess.STDOUT, check=False,
        )


def main():
    args = sys.argv[1:]
    global C_OUT, RUST_OUT
    if "--c" in args:
        C_OUT = args[args.index("--c") + 1]
    if "--rust" in args:
        RUST_OUT = args[args.index("--rust") + 1]
    if "--run" in args or not (os.path.exists(C_OUT) and os.path.exists(RUST_OUT)):
        run_c()
        run_rust()
    c = parse_c(C_OUT)
    rust = {norm(k): v for k, v in parse_rust(RUST_OUT).items()}

    rows = []
    for name in sorted(c):
        if name in rust:
            rows.append((name, rust[name], c[name]))
        else:
            rows.append((name, None, c[name]))

    print(f"{'workload':52} {'rust ns':>12} {'c ns':>12} {'c/rust':>9}")
    print("-" * 90)
    worse = []
    for name, r, cv in rows:
        if r is None:
            print(f"{name:52} {'-':>12} {cv:12.3f} {'-':>9}")
            continue
        ratio = cv / r if r > 0 else float("inf")
        flag = "" if ratio <= 1.25 else "  <-- slower"
        print(f"{name:52} {r:12.3f} {cv:12.3f} {ratio:8.2f}x{flag}")
        if ratio > 1.25:
            worse.append((name, ratio))
    print("-" * 90)
    if worse:
        print("over 1.25x vs Rust:")
        for name, ratio in worse:
            print(f"  {name}: {ratio:.2f}x")
    else:
        print("all workloads within 1.25x of Rust")
    return 0


if __name__ == "__main__":
    sys.exit(main())
