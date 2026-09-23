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
| cell (SemiRefCell) | C validated | migration | header-only macro cells | borrow sequences |
| arena (core+scratch) | C validated | migration | LIFO debug-only | alloc/grow/shrink/scratch tests |
| arena (vec+string) | C validated | migration | generic macro vecs | Rust lossy parity |
| icu (fold+ascii cmp) | C validated | migration | dlopen + version suffix | ICU-backed vectors |
| icu (conv/collation) | C validated | migration | all-or-nothing load | round-trip + collation |
| icu (text/regex UText) | C validated | migration | provider over doc iface | differential search |
| fuzzy (scorer) | C validated | migration | dead Rust code, validated via scratch copy | 27 differential vectors |
| sys (unix/vm) | C started | migration | vm done; rest pending | reserve/commit/release tests |
| buffer (TextBuffer) | Not assessed | migration | undo/redo, regex search | TBD |
| buffer (tbuf core) | C validated | migration | cursor/history/edit/undo | scripted Rust parity |
| buffer (tbuf search) | C validated | migration | select + find/replace | Rust vectors + hang fix |
| buffer (tbuf indent) | C validated | migration | empty-line no-op hardens panic | Rust vectors |
| buffer (tbuf render) | C validated | migration | margin/wrap/select/ruler | byte-exact VT |
| buffer (tbuf file) | C validated | migration | fd IO + BOM + heuristics | round-trip parity |
| buffer (nav) | C validated | migration | differential tables | Rust unit port + select |
| buffer (gap) | C validated | migration | VM + heap backings | ops/copy/nav integration |
| unicode (utf8 decoder) | C validated | migration | WHATWG resync offsets | Rust vector parity |
| unicode (tables) | C validated | migration | generated, bit-verified | lookup vectors |
| unicode (measure) | C validated | migration | i64 compare guards | all 15 Rust tests |
| simd (memchr2/memrchr2/memset) | C validated | migration | scalar first; SIMD needs benches | ports + guard-page tests |
| path (normalize, unix) | C validated | migration | windows deferred to sys slice | unix vectors + extras |
| framebuffer/tui/input | Not assessed | migration | syscalls; concurrency | TBD |
| framebuffer | C validated | migration | diff render, wide glyphs | byte-exact VT |
| input | C validated | migration | SGR/x10/paste quirks | differential events |
| tui (tree core) | C validated | migration | arena nodes, hashmap, DFS | Rust ID chains |
| tui (layout) | C validated | migration | tables/scroll/floats | hand-traced rects |
| tui (uitext) | C validated | migration | ellipsis + styled chunks | byte-exact VT |
| tui (frame) | C validated | migration | input/focus/settling/clipboard | frame pump tests |
| vt (parser) | C validated | migration | DCS quirk mirrored | differential streams |
| sys (rest: console, files, icu-load) | Not assessed | migration | platform ABI | TBD |
| sys (unix tty) | C validated | migration | PTY-tested poll/modes | read/write/resize |
| document (traits) | C validated | migration | vtable + slice doc | interface tests |
| fuzzy (windows paths) | Not assessed | migration | needs sys slice | TBD |

## Validated slices (this checkpoint)

- hash: 117 checks, Rust differential vectors, ASan/UBSan clean.
- base64: 234 checks, port of Rust `test_basic` + binary/error paths, ASan/UBSan clean.
- apperr/helpers: 68 checks, metric output verified equal to Rust, ASan/UBSan clean.
- oklab: 101 checks, Lab/blend/round-trip vs Rust refs, ASan/UBSan clean.
- simd: 3164 checks, ports of memchr2/memrchr2/memset tests + mmap guard pages, ASan/UBSan clean.
- utf8: 301 checks, 24 Rust-vector sequences incl. resync offsets, ASan/UBSan clean.
- path: 98 checks, unix vectors + 15 extras verified equal to Rust, ASan/UBSan clean.
- cell: 11 checks, borrow sequences in debug + release builds.
- vm: 4104 checks, reserve/commit/release incl. Sys(ENOMEM) failure parity.
- arena: 141 checks, alloc/grow/shrink/reset/scratch + NDEBUG run, ASan/UBSan clean.
- avec/astring: 349 checks, generic vec + string ops + lossy parity, ASan/UBSan/NDEBUG clean.
- icu: 65 checks, ICU-backed fold vectors + ascii collation port, ASan clean.
- icu conv/collation: 99 checks total, converter round-trips + root collation, ASan/NDEBUG clean.
- fuzzy: 144 checks, 27 differential vectors from scratch-wired Rust copy, ASan/NDEBUG clean.
- doc/nav: 135 checks, Rust unit port + full offset tables for fwd/bwd/select, ASan/NDEBUG clean.
- gap: 152 checks, small+large backings, copy/nav integration, ASan/NDEBUG clean.
- ucd: 47 checks, tables bit-identical to Rust, ASan/NDEBUG clean.
- measure: 100 checks, all 15 Rust tests ported, ASan/NDEBUG clean.
- vt: 225 checks, differential token streams incl. chunk splits, ASan/NDEBUG clean.
- uregex: 53 checks, UText provider + search over gap buffer, ASan/NDEBUG clean.
- tbuf: 172 checks, scripted edit/undo/cursor/newline parity, ASan/NDEBUG clean.
- tbuf search: 66 checks, select/extract/find/replace parity, ASan/NDEBUG clean.
- tbuf indent/file: 191 + 61 checks, indent vectors + BOM/heuristic/round-trips, ASan/NDEBUG clean.
- tbuf render: 21 checks, byte-exact VT incl. margin/ruler/select, ASan/NDEBUG clean.
- fb: 33 checks, byte-exact VT diff output, ASan/NDEBUG clean.
- input: 122 checks, differential key/mouse/paste/resize events, ASan/NDEBUG clean.
- tree: 37 checks, Rust ID chains + traversal/map semantics, ASan/NDEBUG clean.
- tlayout: 45 checks, stack/table/scroll/float rects, ASan/NDEBUG clean.
- uitext: 52 checks, ellipsis modes + styled chunks, ASan/NDEBUG clean.
- tuiframe: 47 checks, resize/focus/mouse/click/clipboard flows, ASan/NDEBUG clean.
- tty: 39 checks, PTY-backed modes/poll/resize/file-id/lang, ASan/NDEBUG clean.
- Notable: upstream find_and_replace_all loops forever (stale ICU chunks
  after setUText); C refreshes the provider window on set_text and terminates.
- Policy: public arg validation returns errors gracefully (tested in debug);
  asserts kept only for usage-discipline invariants (borrow order, tail-only shrink).
- Gates: strict warnings, cppcheck, clang-format, `cargo +nightly test --lib` green (37).

## Baseline perf

- Rust benchmarks in `benches/` via criterion; C benches pending.
- Rule: no C slice claims perf without measured comparison.

## Gates (C)

- `-Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wnull-dereference -Wdouble-promotion -Wformat=2 -std=c17`
- `clang-format`, `cppcheck`, ASan/UBSan clean, `cargo +nightly test` still green.
