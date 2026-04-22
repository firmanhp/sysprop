# sysprop — Implementation Internals

This document covers architecture, design decisions, and project structure for
contributors and integrators who need to understand the internals.

---

## Why File-per-Property

Common alternatives and why they were rejected:

| Alternative | Problem |
|---|---|
| Android bionic property system | Deeply coupled to Android init, bionic libc, SELinux. Not portable. |
| systemd `hostnamed` / D-Bus | Requires daemon + D-Bus stack; ~µs–ms per read due to IPC. |
| Redis / SQLite on tmpfs | Requires a server process or a WAL — massive overkill. |
| Environment variables | Per-process only; not shared across unrelated processes. |
| `/proc/sys/` sysctl | Kernel-only; cannot add userspace properties. |

**File-per-property in tmpfs** gives:
- Three syscalls per read (`open`/`read`/`close`)
- Atomic writes via POSIX `rename(2)` — no partial reads possible
- No daemon, no shared memory segment, no external runtime dependency

---

## Storage Architecture

```
Public API (sysprop_get / sysprop_set / ...)
    │
    ▼
GlobalStore (singleton, placement-new'd before main())
    │
    ▼
FilePropertyStore  ← policy layer (ro.*, persist.*, validation)
    ├── FileBackend (runtime_)    → /run/sysprop/props/
    └── FileBackend (persistent_) → /etc/sysprop/persistent/  [optional]
```

`FileBackend` is `final` with no virtual interface — `FilePropertyStore` holds
`FileBackend*` directly, eliminating vtable overhead on the I/O path. One vtable
dispatch occurs at the `PropertyStore` interface (policy layer), but property
access is not a tight hot path.

### Atomic writes

`FileBackend::Set` writes the value to `.tmp.<key>.<pid>` in the same directory,
then calls `rename(2)` to atomically replace the target file. `rename(2)` is
atomic on the same filesystem by POSIX: a concurrent reader sees either the old
or the new value, never a partial write. This is a security property — do not
change the write path.

### Key as filename

Keys are constrained to `[a-zA-Z0-9._-]` — the exact set that makes a key safe
to use directly as a POSIX filename without encoding. `ValidateKey` enforces
this. Do not relax the charset.

---

## Persistence Model

**`persist.*` properties live entirely in the persistent backend.** All
`Get`/`Set`/`Delete`/`Exists` calls for `persist.*` keys bypass the runtime
backend and go directly to `SYSPROP_PERSISTENT_DIR`. They are never copied to
the runtime tmpfs. Consequently:

- Processes read `persist.*` from disk on every access.
- No "reload on boot" step is needed — the persistent directory survives reboots
  naturally.
- If the persistent backend is not configured (`SYSPROP_PERSISTENT_DIR=""`),
  `persist.*` falls back to the runtime backend.

**`ro.*` and volatile properties** live only in the runtime tmpfs. Because tmpfs
is cleared on reboot, any `ro.*` properties needed across reboots must be
re-set from a defaults file on every boot (the job of `sysprop-init`).

`ro.*` properties may only be written via `PropertyStore::SetInit()`, which is
called exclusively by `LoadDefaultsFile()` inside `sysprop-init`. The regular
`PropertyStore::Set()` unconditionally rejects `ro.*` keys with
`SYSPROP_ERR_READ_ONLY` — even on the first call. `SetInit()` itself is
write-once: the second call for the same key returns `SYSPROP_ERR_READ_ONLY`.

---

## Auto-Initialization Singleton

`sysprop.cpp` constructs a `GlobalStore` object into static storage using
placement-new inside an `__attribute__((constructor))` function. This runs
before `main()` and before any static-initializer ordering issues can arise:

```cpp
alignas(GlobalStore) unsigned char s_storage[sizeof(GlobalStore)];
GlobalStore* s_instance = nullptr;

__attribute__((constructor))
void sysprop_auto_init() {
    if (s_instance) return;
    s_instance = new (s_storage) GlobalStore{};
}
```

- **No heap allocation** — storage is static.
- **No `std::call_once` / `__cxa_guard_*`** — the constructor attribute runs in
  a single-threaded context before any user threads start.
- **Never destructed** — lifetime matches the process; the OS reclaims on exit.

The directories and persistence flag are compile-time constants (CMake cache
variables), so there is no runtime configuration path.

For testing, `swap_store()` (declared in `include/sysprop/testing/internal.h`)
atomically replaces the global store pointer with a test-supplied `PropertyStore`.

---

## No Exceptions, No Heap on Hot Paths

`-fno-exceptions` is set in `CMakeLists.txt`. All error paths use integer return
codes (`SYSPROP_OK = 0`, negative values for errors).

`Get` and `Set` use only stack buffers. Path construction uses `snprintf` into
`char[]`, never `std::string`. The `std::string sysprop_get(key, default_value)`
overload in the header allocates, but it is a C++ convenience wrapper — not the
core hot path.

---

## Project Structure

```
sysprop/
├── include/sysprop/
│   ├── sysprop.h               ← public C/C++ API (single header)
│   ├── property_store.h        ← abstract PropertyStore interface (internal use)
│   └── testing/
│       ├── internal.h          ← swap_store() for test injection
│       └── mock_property_store.h  ← MockPropertyStore (in-memory, RAII injection)
├── src/
│   ├── sysprop.cpp             ← singleton, C API implementation
│   ├── file_property_store.cpp/h  ← policy layer (ro.*, persist.*, validation)
│   ├── file_backend.cpp/h      ← raw POSIX I/O (open/read/write/rename/unlink)
│   └── validation.cpp/h        ← ValidateKey / ValidateValue
├── tools/
│   ├── sysprop_main.cpp        ← busybox-style CLI (sysprop / getprop / setprop)
│   ├── sysprop_init.cpp        ← boot-time initializer
│   ├── cli_commands.cpp/h      ← CmdGetprop, CmdSetprop, CmdDelete
│   ├── defaults_loader.cpp/h   ← LoadDefaultsFile (key=value parser)
│   └── init_helpers.cpp/h      ← MkdirP, CleanupTmpFiles, ParseInitArgs
├── tests/
│   ├── validation_test.cpp
│   ├── file_backend_test.cpp
│   ├── file_property_store_test.cpp
│   ├── mock_property_store_test.cpp
│   ├── integration_test.cpp
│   ├── sysprop_init_test.cpp
│   ├── sysprop_init_helpers_test.cpp
│   └── sysprop_main_test.cpp
├── benchmarks/
│   └── sysprop_benchmark.cpp
└── examples/
    ├── c_example/main.c
    └── cpp_example/main.cpp
```

---

## What `cmake --install` Does Not Install

`cmake --install` installs only the CLI tools (`sysprop`, `sysprop-init`,
`getprop`/`setprop` symlinks). The library (`libsysprop.a`) and headers have no
install rules.

To consume the library from a non-CMake build system, build it first and
reference the artifacts directly:

```sh
cmake -B /tmp/sysprop-build \
    -DSYSPROP_BUILD_TOOLS=OFF \
    -DSYSPROP_BUILD_TESTS=OFF
cmake --build /tmp/sysprop-build

# C
cc -I/path/to/sysprop/include main.c \
   /tmp/sysprop-build/src/libsysprop.a -o my_app

# C++
c++ -std=c++17 -I/path/to/sysprop/include main.cpp \
   /tmp/sysprop-build/src/libsysprop.a -o my_app
```

---

## Comparison: Storage Approaches

| Approach | Read latency | Write latency | Complexity | Atomicity |
|---|---|---|---|---|
| **File-per-property in tmpfs** (current) | ~1–5 µs (3 syscalls) | ~5–10 µs (write + rename) | Low | `rename(2)` is atomic |
| Shared memory + seqlock | ~10–50 ns (no syscalls) | ~µs (socket IPC to writer) | High | seqlock on serial field |
| Single mmap'd file + hash table | ~50–100 ns | ~100–500 ns | Medium | seqlock per slot |

The file-per-property approach is adequate for embedded systems where property
access is not a tight hot path. The backend is abstracted (`FileBackend` is
swappable) to allow a future mmap-based implementation without changing the
public API.
