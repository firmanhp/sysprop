# sysprop — Claude Code Guide

## Commands

Always use the scripts, not raw cmake/ctest:

```bash
scripts/build.sh              # build library + CLI tools
scripts/test.sh               # build + run full test suite (130 tests)
scripts/test.sh -R FileBackend  # run only tests matching a pattern
scripts/benchmark.sh          # build + run benchmarks
scripts/lint.sh               # run clang-tidy on src/ only
```

Build output: `build/src/libsysprop.a`, `build/tools/sysprop`, `build/tools/sysprop-init`.

## Architecture

Three internal layers, all in `src/` (not part of the public API):

```
include/sysprop/sysprop.h   ← public C API (sysprop_get/set/delete/init)
src/sysprop.cpp             ← global singleton, typed helpers (get_int/bool/float)
src/property_store.cpp/h    ← policy: ro.* immutability, persist.* dual-write
src/file_backend.cpp/h      ← raw POSIX I/O: open/read/write/rename/unlink
src/validation.cpp/h        ← key/value validation only
```

`backend.h` does **not** exist — there is no abstract backend interface. `PropertyStore` takes `FileBackend*` directly. This was a deliberate decision to eliminate vtable overhead.

## Key Design Decisions

**No vtable.** `FileBackend` is `final`, `PropertyStore` holds `FileBackend*` directly. Do not introduce `virtual` or abstract base classes.

**No exceptions.** `-fno-exceptions` is set in `CMakeLists.txt`. All error paths use integer return codes (`SYSPROP_OK = 0`, negative values for errors). Never use `throw`.

**No heap on hot paths.** `Get`/`Set` use stack buffers only. `snprintf` into `char[]` for path construction, never `std::string`.

**Placement-new singleton.** `sysprop_init()` constructs `GlobalStore` into `alignas`-aligned static storage via placement-new. `GetStore()` returns `nullptr` until initialized — all API functions check this and return `SYSPROP_ERR_NOT_INITIALIZED`. No `std::call_once`, no `__cxa_guard_*`. `sysprop_init()` is called once from `main()` before threads start; no internal thread-safety is needed.

**Atomic writes via `rename(2)`.** `FileBackend::Set` writes to `.tmp.<key>.<pid>` then renames. `rename` atomically replaces even pre-existing symlinks (it replaces the directory entry, not the target). This is a security property — do not change the write path.

**Key charset is `[a-zA-Z0-9._-]`.** This is what makes the key safe to use as a filename directly. `ValidateKey` enforces this. Do not relax it.

**`ro.*` policy uses `Exists()` check**, not filesystem permissions. Bare `ro` (no dot) is mutable. Only keys starting with `"ro."` are read-only.

**`persist.*` dual-write failure is non-fatal.** Runtime backend is source of truth; persistent backend failure logs a warning to stderr but `Set` still returns `SYSPROP_OK`.

## Public API Conventions

- `[[nodiscard]]` must **not** appear on `sysprop.h` or `include/sysprop/types.h`. Internal headers (`src/`) retain it.
- Error codes are `#define` macros (not an enum) so the header is valid C.
- `sysprop_get` returns bytes written (≥ 0) on success, negative error code on failure. Callers must check `>= 0`, not `== SYSPROP_OK`.

## Testing

Tests use real `FileBackend` + `mkdtemp`-created temp dirs. There are no mocks. GoogleTest's `testing::TempDir()` is used as the base for all temp directory templates — never hardcode `/tmp`.

Test files and what they cover:
- `validation_test.cpp` — `ValidateKey` / `ValidateValue` only; no I/O
- `file_backend_test.cpp` — raw storage, atomicity, concurrency, filesystem attacks
- `property_store_test.cpp` — policy enforcement (ro, persist, validation delegation)
- `integration_test.cpp` — end-to-end through the C API; includes `GlobalApiTest` (singleton, typed helpers) and evil-attacker scenarios
- `sysprop_init_test.cpp` — `LoadDefaultsFile` (build.prop parsing, validation, ro/persist semantics)
- `sysprop_main_test.cpp` — CLI commands: `DoList`, `CmdGetprop`, `CmdSetprop`, `CmdDelete`
- `sysprop_init_helpers_test.cpp` — `MkdirP`, `CleanupTmpFiles`, `ParseInitArgs`

`GlobalApiTest` uses `SetUpTestSuite` (static) because the singleton can only be initialized once per process. Each test uses a unique key prefix to avoid cross-test interference.

## Clang-Tidy

Lints `src/` only (`HeaderFilterRegex: '.*/sysprop/(src|include)/.*'`). Tests are not linted.

Suppress findings inline at the offending line, not in `.clang-tidy`:
```cpp
::open(path, O_RDONLY | O_CLOEXEC); // NOLINT(cppcoreguidelines-pro-type-vararg)
```

Include the check name so suppressions are auditable. Do not add blanket suppressions to `.clang-tidy`.
