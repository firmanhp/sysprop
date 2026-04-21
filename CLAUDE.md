# sysprop — Claude Code Guide

## Commands

Always use the scripts, not raw cmake/ctest:

```bash
scripts/build.sh              # build library + CLI tools
scripts/test.sh               # build + run full test suite (244 tests)
scripts/test.sh -R FileBackend  # run only tests matching a pattern
scripts/benchmark.sh          # build + run benchmarks
scripts/lint.sh               # run clang-tidy on src/ only
```

Build output: `build/src/libsysprop.a`, `build/tools/sysprop`, `build/tools/sysprop-init`.

## Architecture

Internal layers in `src/` (not part of the public API):

```
include/sysprop/sysprop.h               ← public C API (sysprop_get/set/delete/init)
include/sysprop/property_store.h        ← abstract PropertyStore interface
include/sysprop/testing/internal.h      ← swap_store() for test injection
include/sysprop/testing/mock_property_store.h  ← MockPropertyStore: in-memory mock, RAII injection
src/sysprop.cpp                         ← global singleton, typed helpers (get_int/bool/float)
src/file_property_store.cpp/h           ← FilePropertyStore: policy (ro.*, persist.*) + FileBackend
src/file_backend.cpp/h                  ← raw POSIX I/O: open/read/write/rename/unlink
src/validation.cpp/h                    ← key/value validation only
```

`FileBackend` is `final` with no virtual interface — `FilePropertyStore` holds `FileBackend*` directly, eliminating vtable overhead on the I/O path. `PropertyStore` is an abstract interface; one vtable dispatch occurs at the policy layer but property access is not a tight hot path.

## Key Design Decisions

**Vtable at policy layer only.** `FileBackend` is `final`; `FilePropertyStore` holds `FileBackend*` directly (no vtable on hot I/O path). `PropertyStore` is an abstract interface for `FilePropertyStore` (filesystem) and `MockPropertyStore` (in-memory). Do not introduce vtables below `FilePropertyStore`.

**No exceptions.** `-fno-exceptions` is set in `CMakeLists.txt`. All error paths use integer return codes (`SYSPROP_OK = 0`, negative values for errors). Never use `throw`.

**No heap on hot paths.** `Get`/`Set` use stack buffers only. `snprintf` into `char[]` for path construction, never `std::string`.

**Placement-new singleton.** `sysprop_init()` constructs `GlobalStore` into `alignas`-aligned static storage via placement-new. `GetStore()` returns `nullptr` until initialized — all API functions check this and return `SYSPROP_ERR_NOT_INITIALIZED`. No `std::call_once`, no `__cxa_guard_*`. `sysprop_init()` is called once from `main()` before threads start; no internal thread-safety is needed.

**Atomic writes via `rename(2)`.** `FileBackend::Set` writes to `.tmp.<key>.<pid>` then renames. `rename` atomically replaces even pre-existing symlinks (it replaces the directory entry, not the target). This is a security property — do not change the write path.

**Key charset is `[a-zA-Z0-9._-]`.** This is what makes the key safe to use as a filename directly. `ValidateKey` enforces this. Do not relax it.

**`ro.*` policy uses `Exists()` check**, not filesystem permissions. Bare `ro` (no dot) is mutable. Only keys starting with `"ro."` are read-only.

**`persist.*` operations go directly to persistent backend.** Get/Set/Delete/Exists for `persist.*` keys bypass the runtime backend entirely. If no persistent backend is configured, operations fall back to runtime. `LoadPersistentProperties()` skips `persist.*` keys (accessed directly) but loads other persistent keys (e.g. factory `ro.*`) into runtime.

## Public API Conventions

- `[[nodiscard]]` must **not** appear on `sysprop.h` or `include/sysprop/types.h`. Internal headers (`src/`) retain it.
- Error codes are `#define` macros (not an enum) so the header is valid C.
- `sysprop_get` returns bytes written (≥ 0) on success, negative error code on failure. Callers must check `>= 0`, not `== SYSPROP_OK`.

## Testing

Most tests use real `FileBackend` + `mkdtemp`-created temp dirs. `MockPropertyStore` is available for tests that need in-memory injection without filesystem I/O. GoogleTest's `testing::TempDir()` is used as the base for all temp directory templates — never hardcode `/tmp`.

Test files and what they cover:
- `validation_test.cpp` — `ValidateKey` / `ValidateValue` only; no I/O
- `file_backend_test.cpp` — raw storage, atomicity, concurrency, filesystem attacks
- `file_property_store_test.cpp` — policy enforcement (ro, persist, validation delegation)
- `mock_property_store_test.cpp` — in-memory mock: direct usage and RAII injection into `sysprop_xxx()` API
- `integration_test.cpp` — end-to-end through the C API; includes `GlobalApiTest` (singleton, typed helpers) and evil-attacker scenarios
- `sysprop_init_test.cpp` — `LoadDefaultsFile` (build.prop parsing, validation, ro/persist semantics)
- `sysprop_main_test.cpp` — CLI commands: `DoList`, `CmdGetprop`, `CmdSetprop`, `CmdDelete`
- `sysprop_init_helpers_test.cpp` — `MkdirP`, `CleanupTmpFiles`, `ParseInitArgs`

`GlobalApiTest` uses `SetUpTestSuite` (static) because the singleton can only be initialized once per process. Each test uses a unique key prefix to avoid cross-test interference.

## Clang-Tidy

Lints `src/` only (`HeaderFilterRegex: '.*/sysprop/(src|include)/.*'`). Tests are not linted.

Suppress findings in `.clang-tidy` by removing the check from the `Checks:` list. Inline `NOLINT` comments are reserved for truly isolated one-off cases; if a check fires on more than ~10 sites, disable it globally.
