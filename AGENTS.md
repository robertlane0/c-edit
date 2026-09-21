# AGENTS.md — Rust → Production-Standard Safe C

## Mission

Systematically convert this repository from **Rust to production-quality safe C** while preserving externally observable behavior and eliminating all unsafe constructs.

The finished C implementation must be:

- **Safe C only**: No undefined behavior, no buffer overflows, no use-after-free, no double-frees, no null pointer dereferences, no integer overflows without explicit handling.
- Production-ready: correct, tested, observable, maintainable, performant enough for the workload, and operationally documented.
- Behavior-compatible with the Rust implementation unless an intentional incompatibility is explicitly documented and approved.
- Free of Rust FFI in the production C path unless a separate, explicitly approved migration boundary exists.
- Written in modern C (C11/C17/C23 as appropriate) with strict compiler warnings enabled.

Do not treat "it compiles" as completion.

---

## Non-Negotiable Rules

1. **Never introduce undefined behavior.**
   - Do not use unchecked pointer arithmetic, buffer overflows, use-after-free, double-free, null dereferences, or integer overflows as shortcuts.
   - All dynamic allocations must have clear ownership and deallocation paths.
   - All pointers must be validated before use.
   - All array accesses must be bounds-checked.
   - Do not hide unsafety behind macros, generated code, or build scripts merely to evade review.

2. **Preserve semantics before redesigning.**
   - First reproduce the Rust behavior.
   - Then improve architecture, APIs, or algorithms in separately reviewable changes.
   - Document deliberate behavior changes.

3. **Migrate incrementally.**
   - Keep the repository buildable and testable at every meaningful checkpoint.
   - Prefer small vertical slices over a large mechanical rewrite.
   - Each migrated component must have a clear owner, test strategy, and rollback/containment strategy.

4. **Do not silence correctness signals.**
   - Do not weaken compiler warnings, delete failing tests, broadly suppress warnings, or relax CI gates to make migration pass.
   - Every exception must be narrowly scoped, justified, and tracked.

5. **Measure production behavior.**
   - Validate correctness, latency, throughput, memory use, startup behavior, resource consumption, and failure behavior where relevant.
   - Performance claims must be supported by representative benchmarks or profiling.

---

## Migration Workflow

For every component, follow this order unless there is a documented reason not to.

### 1. Inventory

Before changing code, identify:

- Public APIs and externally observable behavior.
- Callers and dependencies.
- Threading and synchronization assumptions.
- Ownership and lifetime rules.
- Error handling behavior.
- Input validation and boundary conditions.
- Serialization, wire, file, and ABI formats.
- Resource ownership: files, sockets, locks, processes, memory, handles, etc.
- Platform-specific behavior.
- Build flags and generated code.
- Existing tests, benchmarks, fuzz targets, and production diagnostics.
- Security-sensitive operations.
- Rust-specific hazards (panics, unwinding, borrow checker guarantees).

Record important findings in migration notes or issue tracking rather than relying on tribal knowledge.

### 2. Characterize the Rust behavior

Before translating implementation details, establish a behavioral specification from:

1. Existing tests.
2. Public documentation/contracts.
3. Call sites.
4. Runtime observations.
5. Focused characterization tests added during migration.

Pay special attention to behavior that Rust enforces but callers may rely on (e.g., panic on invalid input, guaranteed initialization, thread-safety guarantees).

### 3. Design the safe C boundary

Map Rust concepts to idiomatic safe C:

| Rust concept | Preferred C model |
|---|---|
| `Box<T>` | `malloc`/`free` with clear ownership, or stack allocation |
| `Vec<T>` | Struct with pointer, length, capacity; or fixed array if bounded |
| `String`/`&str` | `char*` with explicit length, or null-terminated with documented convention |
| `Option<T>` | Pointer with NULL check, or tagged union, or sentinel value |
| `Result<T, E>` | Return code + output parameter, or struct with tag + union |
| `Arc<T>`/`Rc<T>` | Reference-counted struct with explicit increment/decrement |
| `&T`/`&mut T` | Pointer with documented aliasing rules (`restrict` where applicable) |
| `enum` | `enum` with explicit discriminants, or tagged union |
| Pattern matching | `switch` on enum tag, or if-else chain |
| Traits | Struct with function pointers (vtable), or explicit dispatch |
| Generics | `_Generic` (C11), macros, or code generation |
| Closures | Struct with context pointer + callback function |
| `Mutex<T>`/`RwLock<T>` | `pthread_mutex_t`/`pthread_rwlock_t` with documented ownership |
| Atomic types | `stdatomic.h` with explicit memory ordering |
| Thread-local | `_Thread_local` (C11) or platform-specific TLS |
| `Drop` trait | Explicit cleanup function (`*_free`, `*_destroy`) |
| Panic | Return error code, log, and abort/exit, or longjmp if justified |
| `unwrap()`/`expect()` | Assert in debug, return error in production |
| Iterator | Struct with state + next function, or index-based loop |
| Slice `&[T]` | Pointer + length pair |
| Newtype | `typedef` or wrapper struct for type safety |
| Module/crate | Translation unit (`.c`/`.h`) with clear interface |
| `unsafe` block | Eliminate; redesign to avoid the hazard |
| Borrow checker guarantees | Document lifetime/aliasing contracts explicitly |
| Typestate pattern | Explicit state field + validation functions |
| RAII | Cleanup labels with `goto`, or explicit destroy functions |

Do not mechanically translate syntax. Translate **ownership, invariants, state transitions, and contracts**.

### 4. Implement the smallest vertical slice

A slice should ideally include:

- C implementation (`.c`) and header (`.h`).
- Unit tests.
- Integration/characterization tests where applicable.
- Error handling.
- Logging/metrics/tracing needed for production diagnosis.
- Documentation for non-obvious invariants.
- Benchmark coverage if performance-sensitive.

### 5. Differential validation

Where practical, run equivalent inputs through Rust and C and compare:

- Return values.
- Serialized/wire output.
- State transitions.
- Errors and error categories.
- Ordering guarantees.
- Boundary behavior.
- Resource behavior.
- Performance characteristics.

For nondeterministic systems, compare defined invariants rather than incidental ordering.

### 6. Remove the old implementation

Only remove Rust code after:

- C behavior is sufficiently characterized.
- Required tests pass.
- Production-relevant observability exists.
- Performance is acceptable.
- No remaining callers depend on the old path.
- Build/package/release tooling no longer requires it.
- Migration tracking is updated.

Avoid leaving dead Rust and C implementations indefinitely.

---

## C Standards

Use modern C (C11/C17/C23) with strict compiler warnings.

Recommended baseline:

- GCC/Clang with `-Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wnull-dereference -Wdouble-promotion -Wformat=2`
- Static analysis: `clang-tidy`, `cppcheck`, or equivalent
- Sanitizers in CI: ASan, USan, TSan, MSan where applicable
- Release-mode tests/benchmarks where relevant.
- Documentation generation (e.g., Doxygen) for public APIs.

Configure the workspace so accidental undefined behavior fails CI.

Use strong typing to make invalid states difficult or impossible to represent.

Prefer:

- Explicit error codes instead of silent failures.
- `NULL` checks before pointer dereference.
- Bounds checking on all array/slice access.
- `size_t` for sizes and indices, with overflow checks.
- `bool` from `<stdbool.h>` for boolean values.
- Fixed-width integers from `<stdint.h>` for serialization/ABI.
- `static` for internal linkage.
- `const` correctness throughout.
- `restrict` for pointers with no aliasing.
- Explicit initialization of all variables.
- Clear ownership comments for dynamically allocated memory.
- `goto` for centralized cleanup in complex functions (when clearer than multiple exit points).

Avoid:

- Implicit conversions that lose information.
- Magic numbers without named constants.
- Global mutable state without explicit synchronization.
- Recursive functions without depth limits.
- Variable-length arrays in production code.
- Bit-field layouts for cross-platform serialization.
- Unchecked casts between unrelated pointer types.
- Macro abuse that obscures control flow.

---

## Rust Semantics That Require Special Attention

### Ownership and lifetime

Rust's borrow checker enforces what C leaves implicit.

For every reference/pointer in the source, determine:

- Who owns the object?
- How long must it live?
- Can it be null?
- Can it be mutated?
- Can aliases coexist?
- Is ownership transferred?
- Is destruction observable?

Then choose a C representation that encodes those facts via:

- Explicit documentation.
- Naming conventions (`_create`, `_destroy`, `_borrow`, `_clone`).
- Static analysis annotations where available.
- Runtime assertions in debug builds.

### Panics and unwinding

Rust panics are not exceptions.

Distinguish:

- Programmer errors (invariant violations): assert in debug, document as undefined behavior in release, or abort.
- Expected operational failures: return error codes.
- External infrastructure failures: return error codes with context.
- Security violations: fail securely, log appropriately, abort if necessary.

Do not use `longjmp` to simulate unwinding unless absolutely necessary and fully documented.

### Zero-initialization and MaybeUninit

Rust's `MaybeUninit` and zero-initialization guarantees must be handled carefully.

In C:

- Explicitly initialize all struct fields.
- Use `memset` only for POD types with no padding issues.
- Document when uninitialized memory is used intentionally (e.g., for performance with subsequent full initialization).
- Be aware of padding bytes and their impact on serialization/comparison.

### Concurrency

Rust's type system prevents data races at compile time. C requires runtime discipline.

Explicitly reason about:

- Thread ownership of objects.
- Lock ordering.
- Deadlocks.
- Cancellation.
- Shutdown.
- Atomic memory ordering.
- Backpressure.
- Thread lifetime.
- Memory visibility and cache coherence.

Prefer message passing or clear ownership transfer when it simplifies the concurrency model.

Use `stdatomic.h` for atomic operations with explicit memory ordering.

### ABI, wire, and binary formats

Treat all externally visible formats as contracts.

For each format, test:

- Field order.
- Width and signedness.
- Endianness.
- Alignment/padding.
- Encoding.
- Versioning.
- Optional fields.
- Malformed input.
- Round-trip behavior.

Do not rely on struct layout for serialization; use explicit serialization functions.

### Integer overflow

Rust has checked arithmetic in debug mode and wraps in release (with overflow checks available).

In C:

- Use `<stdint.h>` fixed-width types for explicit sizing.
- Check for overflow before operations or use compiler builtins (`__builtin_add_overflow`, etc.).
- Document when wrapping behavior is intentional.
- Prefer wider intermediate types for calculations.

---

## Testing Requirements

Every migrated component must have tests appropriate to its risk.

### Minimum

- Existing relevant tests continue to pass or are faithfully ported.
- New tests cover newly encoded invariants.
- Boundary cases are tested.
- Error paths are tested.
- Regression tests are added for migration-discovered bugs.

### Higher-risk components

Use additional techniques where appropriate:

- Property-based tests.
- Fuzzing for parsers and untrusted input.
- Differential tests against the Rust implementation.
- Stress tests for concurrency.
- Sanitizer-assisted testing (ASan, USan, TSan, MSan).
- Benchmarks and profiling.
- Fault injection.
- Long-running soak tests.
- Valgrind/AddressSanitizer runs for memory correctness.

Tests must verify externally meaningful behavior, not merely implementation details.

---

## Error Handling

Production errors should be:

- Typed where useful (error codes, error enums).
- Actionable.
- Context-rich.
- Stable enough for callers to classify.
- Free from accidental leakage of secrets or sensitive data.

Do not log:

- Passwords.
- Tokens.
- Private keys.
- Credentials.
- Full sensitive payloads.

Common patterns:

```c
// Return code pattern
int my_function(my_context_t *ctx, output_t *out);

// Error struct pattern
typedef struct {
    int code;
    const char *message;
    const char *file;
    int line;
} error_t;

// Result struct pattern
typedef struct {
    bool ok;
    union {
        result_data_t value;
        error_t error;
    };
} result_t;
```

At application boundaries, produce appropriate exit statuses, HTTP/RPC status codes, or protocol-level errors.

---

## Observability

A production-standard migration must preserve or improve diagnostics.

Where relevant, provide:

- Structured logs.
- Metrics.
- Traces.
- Correlation/request IDs.
- Startup/shutdown diagnostics.
- Resource and queue visibility.
- Useful error context.

Avoid logging in hot loops without evidence that the volume is acceptable.

Instrumentation must not materially alter correctness or create hidden synchronization bottlenecks.

---

## Performance

Do not optimize by intuition alone.

For performance-sensitive migrations:

1. Establish a Rust baseline.
2. Define representative workloads.
3. Benchmark C under equivalent conditions.
4. Profile meaningful regressions.
5. Optimize only the demonstrated bottleneck.
6. Re-run correctness and benchmark suites.

Do not introduce undefined behavior to recover performance.

Accept a small performance regression only when it is understood, measured, and justified by a concrete production requirement.

Leverage C strengths where appropriate:

- Stack allocation for small, bounded objects.
- Contiguous memory layouts for cache efficiency.
- Manual memory management for custom allocators.
- Inline functions for hot paths.
- Restrict pointers for alias analysis.

---

## Dependencies and Supply Chain

Prefer mature, actively maintained libraries with:

- Appropriate licensing.
- Clear ownership/maintenance.
- Small, understandable dependency surface.
- Good security history.
- Compatible C standard policy.
- Safe public APIs.

Before adding a dependency, check whether the standard library or existing workspace dependencies already solve the problem.

Keep dependency versions reproducible where appropriate.

Audit dependency changes for:

- Transitive dependency growth.
- Native code requirements.
- Build system complexity.
- Network access during builds.
- License implications.
- Known vulnerabilities.

---

## Code Review Checklist

A migration PR is not ready until reviewers can answer "yes" to the applicable items:

- [ ] Modern C standard (C11/C17/C23) is used consistently.
- [ ] No undefined behavior was introduced.
- [ ] Rust behavior was characterized before translation.
- [ ] Ownership and lifetimes are explicitly documented.
- [ ] All pointers are validated before use.
- [ ] All array accesses are bounds-checked.
- [ ] Error behavior is intentionally mapped.
- [ ] Concurrency behavior is understood and tested.
- [ ] External formats/ABIs are preserved where required.
- [ ] Tests cover normal, boundary, and failure cases.
- [ ] Differential testing was used where practical.
- [ ] Performance was measured for sensitive paths.
- [ ] Logging/metrics/tracing are adequate.
- [ ] No secrets or sensitive data are exposed in logs.
- [ ] Compiler warnings are resolved rather than suppressed.
- [ ] Static analysis passes cleanly.
- [ ] Sanitizer testing passes (ASan, USan, TSan as applicable).
- [ ] Documentation explains non-obvious invariants.
- [ ] Build/release tooling works without unnecessary Rust dependencies.
- [ ] Dead migration scaffolding is removed.
- [ ] CI gates the relevant standards.
- [ ] The migration tracker is updated.

---

## CI Quality Gates

The C portion of the repository should fail CI when applicable checks fail.

At minimum, establish gates for:

1. Formatting (e.g., `clang-format --dry-run --Werror`).
2. Compilation with strict warnings (`-Wall -Wextra -Wpedantic -Werror`).
3. Tests (unit, integration, end-to-end).
4. Static analysis (clang-tidy, cppcheck).
5. Sanitizer testing (ASan, USan, TSan in CI).
6. Modern C standard compliance.
7. No undefined behavior (verified by sanitizers and static analysis).
8. Documentation/build checks for public libraries.
9. Dependency/security policy checks.
10. Benchmarks or performance thresholds for designated critical paths.

Do not make migration-specific CI exceptions permanent. Every temporary exception needs an owner and removal criterion.

---

## Migration Tracking

Track each component with states such as:

- `Not assessed`
- `Assessed`
- `Characterized`
- `C implementation started`
- `C implementation validated`
- `Shadow/differential validation`
- `Production rollout`
- `Rust removed`
- `Completed`

For each component record, when relevant:

- Scope.
- Dependencies.
- Behavioral risks.
- Compatibility constraints.
- Performance baseline.
- Test coverage.
- Known gaps.
- Rollout plan.
- Owner.
- Exit criteria.

---

## Definition of Done

A component is considered migrated only when all applicable conditions are satisfied:

1. Its production path is implemented in modern safe C.
2. The implementation contains no undefined behavior.
3. Behavior is characterized and validated.
4. Tests cover important success and failure modes.
5. Error handling is production-appropriate.
6. Ownership and lifetime contracts are explicit and documented.
7. Observability is sufficient for operation and diagnosis.
8. Performance is acceptable based on measurement.
9. Security and dependency review is complete.
10. CI enforces the applicable quality gates.
11. The old Rust implementation is no longer required.
12. Documentation and migration tracking are updated.

The ultimate repository-level definition of done is:

> **The Rust implementation has been replaced by maintainable, production-standard safe C, with equivalent or intentionally improved behavior, and the production C codebase contains no undefined behavior.**

