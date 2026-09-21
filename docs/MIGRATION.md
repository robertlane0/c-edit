# Migration tracking (Rust → safe C)

Baseline: `edit` 1.0.0 fork of Microsoft Edit. Nightly Rust, 37 lib + 1 bin + 3 doc tests green.

## States

- `Not assessed` → `Assessed` → `Characterized` → `C started` → `C validated` → `Differential` → `Rollout` → `Rust removed` → `Completed`

## Components

| Component | State | Owner | Risks | Exit criteria |
|---|---|---|---|---|
| hash (wyhash/wymix) | C validated | migration | `unsafe` ptr reads; endianness | unit + differential vs Rust |
| base64 (encode) | C validated | migration | `unsafe` bulk u32 reads; bounds | port of `test_basic` + vectors |
| apperr (Error) | C validated | migration | error mapping | mapping tests |
| helpers (metric/point/size/rect) | C validated | migration | overflow on rect math | geometry tests |
| oklab (srgb/oklab/blend) | C validated | migration | float bit hacks; precision | round-trip + blend tests |
| cell (SemiRefCell) | Assessed | migration | borrow rules → explicit docs | debug assert model |
| arena/* | Characterized | migration | lifetimes; custom alloc | TBD |
| buffer/* | Not assessed | migration | gap buffer invariants | TBD |
| unicode/* | Not assessed | migration | tables; utf8 validation | TBD |
| simd/* | Not assessed | migration | over-read discipline | TBD |
| icu | Not assessed | migration | generated tables | TBD |
| framebuffer/tui/vt/input | Not assessed | migration | syscalls; concurrency | TBD |
| sys (unix/windows) | Not assessed | migration | platform ABI | TBD |
| document/bin/edit | Not assessed | migration | app wiring | TBD |
| fuzzy/path | Not assessed | migration | icu dep; path normalize | TBD |

## Validated slices (this checkpoint)

- hash: 117 checks, Rust differential vectors, ASan/UBSan clean.
- base64: 234 checks, port of Rust `test_basic` + binary/error paths, ASan/UBSan clean.
- apperr/helpers: 68 checks, metric output verified equal to Rust, ASan/UBSan clean.
- oklab: 101 checks, Lab/blend/round-trip vs Rust refs, ASan/UBSan clean.
- Gates: strict warnings, cppcheck, clang-format, `cargo +nightly test --lib` green (37).

## Baseline perf

- Rust benchmarks in `benches/` via criterion; C benches pending.
- Rule: no C slice claims perf without measured comparison.

## Gates (C)

- `-Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wnull-dereference -Wdouble-promotion -Wformat=2 -std=c17`
- `clang-format`, `cppcheck`, ASan/UBSan clean, `cargo +nightly test` still green.
