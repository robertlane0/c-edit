# Inventory (externally observable behavior)

## Public surface

- Binary `edit`: terminal editor, file picker, menubar, statusbar, localization.
- Lib crates: arena, base64, buffer, cell, document, framebuffer, hash, helpers, icu, input, oklab, path, simd, sys, tui, unicode, vt.
- Wire/file formats: document files as-is; base64 output; sRGB u32 `0xAABBGGRR` layout; terminal VT sequences.

## Callers/deps

- Leaf (no internal deps): hash, oklab, cell, apperr, parts of helpers/simd.
- Mid: base64 → arena; fuzzy → arena + icu; path → sys.
- Core: arena → everything; buffer → arena; tui/framebuffer → sys/vt/unicode/oklab.
- External: `libc` on unix; `windows-sys` on windows; `criterion` dev only. No runtime network.

## Threading/sync

- Single-threaded editor; no `Mutex`/`Arc` in scope; `cell::SemiRefCell` = debug-checked aliasing only.
- C model: explicit non-thread-safe docs; no global mutable state.

## Ownership/lifetimes

- `Arena` owns scratch/string/vec memory; `ArenaString` borrows arena.
- `Box/Vec/String`: malloc/free with clear owner or stack buffers.
- `Option`: NULL or tagged union; `Result`: return code + out-param.

## Errors

- `apperr::Error{App,Icu,Sys}(u32)`; `io::Error` → `sys::io_error_to_apperr`.
- Panics: invariant violations only; C maps to `assert` (debug) + error return.

## Validation/boundaries

- base64: `encode_len = div_ceil(n,3)*4`; bulk 4-byte reads need `remaining > 3`.
- hash: unaligned `u32/u64` reads, lengths 0/1-3/4-16/17-48/49+ paths.
- unicode/utf8/vt: untrusted input, needs fuzz + malformed tests.
- rect/point: `i32` coords, safe range ±32767; `width = right-left` can underflow if unchecked.

## Resources/platforms

- Files, console, locks via `sys::{unix,windows}`; Windows manifest in `build.rs`.
- Build flags: nightly features (allocator_api, let_chains, etc.), release `opt-level=s,lto,panic=abort,strip`.

## Tests/diagnostics

- 37 lib tests (simd, unicode, base64, icu, path, buffer), 1 bin, 3 doctests; `benches/` criterion.
- No sanitizers wired yet; C must add ASan/UBSan/TSan + cppcheck + clang-tidy.

## Security

- Parsers (utf8, vt, icu tables, fuzzy haystacks) handle untrusted bytes; no secrets logged.
