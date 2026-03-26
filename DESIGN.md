# sysprop: Linux Embedded System Properties Manager

## Context

Build a system properties management system similar to Android's setprop/getprop for Linux embedded systems. The system provides a global key-value store accessible by all processes, with support for volatile (tmpfs-backed), read-only (`ro.*`), and persistent (`persist.*`) properties. Phase 2 will add security contexts.

**Design principles**: Idiomatic C++17, minimal external dependencies (only gtest/gmock/benchmark for testing), zero heap allocation on hot paths (get/set), integer return codes for compatibility.

---

## Comparison with Off-the-Shelf Solutions

### Existing implementations in the wild

| Solution | Description | Why not suitable |
|---|---|---|
| **Android bionic property system** | mmap shared memory + trie + seqlock. Gold standard performance (~ns reads). Source in AOSP `bionic/libc/system_properties/`. | Deeply coupled to Android init, bionic libc, and SELinux. Not portable to generic Linux without major surgery. |
| **systemd `machined`/`hostnamed`** | D-Bus properties exposed by systemd services. | Requires systemd + D-Bus stack. ~us-ms per read due to IPC. Too heavyweight for embedded. |
| **OpenWrt `ubus`/`libubox`** | Unix socket IPC with central `ubusd` daemon. Lightweight property-like mechanism. | Still IPC per read (~us). C-only API. Tied to OpenWrt ecosystem. |
| **Redis on tmpfs** | In-memory KV store. | Requires server process, Lua runtime, TCP/IP stack. Massive overkill. |
| **SQLite on tmpfs** | ACID KV via SQL. | syscall per read, WAL overhead on writes. Overkill for simple KV. |
| **D-Bus `org.freedesktop.DBus.Properties`** | Standard property interface on any D-Bus service. | Requires dbus-daemon, message serialization overhead, ~us-ms latency. |
| **`/proc/sys/` sysctl** | Kernel-managed file-per-property in procfs. | Kernel-only; cannot add userspace properties. |
| **environment variables** | Per-process, inherited on fork. | Not shared across unrelated processes. No persistence. No change notification. |

### Storage approach comparison

| Approach | Read Latency | Write Latency | Complexity | Atomicity | Notes |
|---|---|---|---|---|---|
| **File-per-property in /run/** | ~1-5 us (3 syscalls) | ~5-10 us (write+rename) | Low | rename() is atomic | Simple, adequate for most embedded. Our Phase 1 choice. |
| Shared memory mmap + seqlock | ~10-50 ns (no syscalls) | ~us (socket IPC to writer) | High | seqlock on serial field | Android's approach. 100x faster reads, but complex shared-memory layout. Phase 2+ candidate. |
| Single mmap'd file + hash table | ~50-100 ns | ~100-500 ns (direct write) | Medium | seqlock per slot | Hybrid: simpler than trie, still lock-free. Good upgrade path. |

**Recommendation: File-per-property in /run/** for Phase 1. Atomic via POSIX `rename()`, simple, zero external deps. Backend is abstracted so mmap can be swapped in later.

**Atomicity model:**
- **Write**: write value to `.tmp.<key>.<pid>`, then `rename()` to final path (atomic on same filesystem)
- **Read**: `open()`/`read()`/`close()` — gets a consistent snapshot since rename atomically swaps the directory entry
- **Single writer assumed** (like Android's init). Multiple concurrent writers are safe (rename is atomic) but last-write-wins.

---

## Core Design (Zero-Allocation Hot Paths)

Keys and values have a 256-byte limit. The API uses raw `char*` + `size_t` — the caller controls allocation (stack, heap, arena — their choice).

```cpp
// include/sysprop/types.h
namespace sysprop {

inline constexpr std::size_t kMaxKeyLength   = 256;
inline constexpr std::size_t kMaxValueLength = 256;

// Integer error codes (negative = error, 0 = success)
enum Error : int {
    OK              =  0,
    ERR_NOT_FOUND   = -1,
    ERR_READ_ONLY   = -2,
    ERR_INVALID_KEY = -3,
    ERR_VALUE_TOO_LONG = -4,
    ERR_KEY_TOO_LONG   = -5,
    ERR_IO          = -6,
    ERR_PERMISSION  = -7,
    ERR_NOT_INITIALIZED = -8,
};

} // namespace sysprop
```

**Public API** (`include/sysprop/sysprop.h`):
```cpp
namespace sysprop {

struct Config {
    const char* runtime_dir    = "/run/sysprop/props";
    const char* persistent_dir = "/etc/sysprop/persistent";
    bool enable_persistence    = true;
};

int Init(const Config& config = {});
// Read property value into caller-provided buffer. Returns bytes written, or Error (<0).
int Get(const char* key, char* buf, std::size_t buf_len);
// Set property value. Returns 0 on success or Error (<0).
int Set(const char* key, const char* value);
int Delete(const char* key);

// Type-conversion helpers (read property, parse as type)
int GetInt(const char* key, int default_value);
bool GetBool(const char* key, bool default_value);
float GetFloat(const char* key, float default_value);

} // namespace sysprop
```

**Internal-only API** (in `src/`, used by tools but not exposed in public headers):
- `int List(...)` — used by `sysprop list` / `getprop` with no args
- `int LoadPersistentProperties()` — used by `sysprop-init`

---

## Project Structure

```
sysprop/
├── CMakeLists.txt
├── cmake/
│   └── FetchDependencies.cmake       # gtest 1.14+, google benchmark 1.8+
├── include/sysprop/
│   ├── types.h                       # Error codes, constants (kMaxKeyLength, kMaxValueLength)
│   └── sysprop.h                     # Public API: Init/Get/Set/Delete + GetInt/GetBool/GetFloat
├── src/
│   ├── CMakeLists.txt
│   ├── validation.h / .cpp           # Key/value validation
│   ├── backend.h                     # Abstract storage backend interface (internal)
│   ├── file_backend.h / .cpp         # File-based /run/ backend (raw POSIX I/O)
│   ├── property_store.h / .cpp       # Business logic (ro.*, persist.* policy)
│   └── sysprop.cpp                   # Public API impl (global singleton)
├── tools/
│   ├── CMakeLists.txt
│   ├── sysprop_main.cpp              # Busybox-style CLI (getprop/setprop/sysprop)
│   └── sysprop_init.cpp              # Boot-time persistent property loader
├── tests/
│   ├── CMakeLists.txt
│   ├── validation_test.cpp
│   ├── file_backend_test.cpp
│   ├── property_store_test.cpp       # Uses gmock for backend mocking
│   └── integration_test.cpp
└── benchmarks/
    ├── CMakeLists.txt
    └── sysprop_benchmark.cpp
```

Note: `backend.h` is internal (in `src/`), not part of the public API. The public interface is just `types.h` + `sysprop.h`.

---

## Implementation Steps

### Step 1: Project Scaffolding
- **`CMakeLists.txt`** — C++17, `-Wall -Wextra -Wpedantic`. Options: `SYSPROP_BUILD_TESTS`, `SYSPROP_BUILD_BENCHMARKS`, `SYSPROP_BUILD_TOOLS` (all default ON).
- **`cmake/FetchDependencies.cmake`** — FetchContent for googletest + google benchmark. Guarded by build options. Also supports `find_package()` fallback for cross-compile toolchains.
- Configurable defaults via CMake cache: `SYSPROP_RUNTIME_DIR` (`/run/sysprop`), `SYSPROP_PERSISTENT_DIR` (`/etc/sysprop`), `SYSPROP_MAX_KEY_LENGTH` (256), `SYSPROP_MAX_VALUE_LENGTH` (256). Baked into `types.h` via `configure_file()` or compile definitions.

### Step 2: Core Types (`include/sysprop/types.h`)
- `enum Error : int` — negative values for errors, 0 for success
- Constants: `kMaxKeyLength = 256`, `kMaxValueLength = 256`
- No custom string types — caller provides `char*` buffers

### Step 3: Validation (`src/validation.h/.cpp`) + Tests
- `int ValidateKey(std::string_view key)` — charset `[a-zA-Z0-9._-]`, no empty segments (no `..`, no leading/trailing dot), length <= 256
- `int ValidateValue(std::string_view value)` — length <= 256, no embedded nulls
- `tests/validation_test.cpp` — valid/invalid keys and values

### Step 4: Backend Interface + File Backend
- **`src/backend.h`** — internal abstract interface:
  ```cpp
  class Backend {
  public:
      virtual ~Backend() = default;
      virtual int Get(const char* key, char* buf, std::size_t buf_len) = 0;  // returns bytes written or Error
      virtual int Set(const char* key, const char* value) = 0;
      virtual int Delete(const char* key) = 0;
      // ForEach calls visitor for each property. Internal use only.
      using Visitor = std::function<void(const char* key, const char* value)>;
      virtual int ForEach(Visitor visitor) = 0;
      virtual int Exists(const char* key) = 0;
  };
  ```
  Note: `ForEach()` replaces `List()` — avoids pre-allocating arrays, lets caller decide what to do with each entry.

- **`src/file_backend.h/.cpp`**:
  - Constructor takes `const char* base_path` (e.g., `/run/sysprop/props`)
  - `Get()`: `open()` → `read()` into caller's buffer → `close()`. 3 syscalls. Returns bytes read.
  - `Set()`: `open(.tmp.<key>.<pid>)` → `write()` → `close()` → `rename()`. 4 syscalls.
  - `Delete()`: `unlink()`. 1 syscall.
  - `ForEach()`: `opendir()` → `readdir()` (skip `.` entries) → read each file → call visitor → `closedir()`.
  - Key used directly as filename — validation ensures charset is filesystem-safe.
  - Path construction: `snprintf()` into stack buffer (no heap).
- **`tests/file_backend_test.cpp`** — `mkdtemp()` for isolation, concurrent read/write stress test

### Step 5: Property Store (Policy Layer)
- **`src/property_store.h/.cpp`**:
  - Holds `Backend* runtime_backend` and optionally `Backend* persistent_backend`
  - `Set()`: validates key/value, enforces `ro.*` (check `Exists()` first, refuse overwrite), calls `runtime_backend->Set()`, then for `persist.*` also calls `persistent_backend->Set()` (failure is non-fatal, logged to stderr)
  - `Get()`: validates key, delegates to `runtime_backend->Get()`
  - `Delete()`: blocked for `ro.*`, cascades to persistent for `persist.*`
  - `LoadPersistentProperties()`: lists persistent backend, sets each into runtime
- **`tests/property_store_test.cpp`** — gmock `MockBackend` for policy unit tests

### Step 6: Public API
- **`include/sysprop/sysprop.h`** — as described in Core Design section above: `Init`, `Get`, `Set`, `Delete`, `GetInt`, `GetBool`, `GetFloat`
- **`src/sysprop.cpp`** — global `PropertyStore` + `FileBackend` instances, initialized via `std::call_once`. Type helpers parse the string result of `Get()` with `strtol`/`strtof`.
- **`tests/integration_test.cpp`** — end-to-end with real file backend in temp dirs

### Step 7: CLI Tools
- **`tools/sysprop_main.cpp`** — Busybox-style dispatch on `argv[0]`:
  - `getprop [name] [default]` / `sysprop get` — print property or list all (`[key]: [value]`)
  - `setprop name value` / `sysprop set` — set property
  - `sysprop delete name` — delete property
  - `sysprop list` — list all properties
  - Config override via `--runtime-dir` / `--persistent-dir` flags or env vars `SYSPROP_RUNTIME_DIR` / `SYSPROP_PERSISTENT_DIR`
- **`tools/sysprop_init.cpp`**:
  - Creates runtime directory (`mkdir -p` equivalent)
  - Cleans up orphan `.tmp.*` files from previous crashes
  - Loads persistent properties from disk into runtime store
  - Optionally loads defaults from a `build.prop` style file (`key=value`, `#` comments)
- **CMake install**: `sysprop` binary to `${CMAKE_INSTALL_BINDIR}`, symlinks `getprop`/`setprop`

### Step 8: Benchmarks
- **`benchmarks/sysprop_benchmark.cpp`**:
  - `BM_Get` / `BM_Set` / `BM_GetMissing` — single-threaded latency
  - `BM_List` — parameterized by N (10, 100, 1000)
  - `BM_ConcurrentReads` — multi-threaded read of same property

---

## Key Design Decisions

1. **Caller-controlled allocation** — `Get()` takes `char* buf, size_t len`. Caller decides stack vs heap. Library never allocates on hot paths.
2. **Integer return codes** — `enum Error : int` with negative error values. Compatible with C ABIs and simple to check.
3. **Raw POSIX I/O** — `open`/`read`/`close`/`rename` directly. No `iostream`, no `std::filesystem` on hot paths. Path construction via `snprintf()` into stack buffers.
4. **Key = filename** — validation constrains charset to `[a-zA-Z0-9._-]`, keys are filesystem-safe with no encoding.
5. **Backend abstraction is internal** — `backend.h` lives in `src/`, not `include/`. Public API is just free functions + types. This keeps the public surface minimal and allows backend swap (e.g., mmap) without API changes.
6. **ro.* is application-level policy** — checked via `Exists()` in PropertyStore, not filesystem chmod. Simpler, no root required.
7. **Persist failure is non-fatal** — runtime tmpfs is source of truth. Persistent storage degradation doesn't break the system.
8. **Minimal dependencies** — only gtest/gmock/benchmark for testing. No protobuf, no boost, no abseil. Just C++17 + POSIX.

---

## Verification

1. **Build**: `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build`
2. **Unit tests**: `cd build && ctest --output-on-failure`
3. **Manual CLI test**:
   ```bash
   ./build/tools/sysprop-init --runtime-dir /tmp/test-sysprop
   ./build/tools/sysprop set test.hello world
   ./build/tools/sysprop get test.hello       # expect: "world"
   ln -s sysprop /tmp/getprop && /tmp/getprop test.hello  # busybox-style
   ```
4. **Persistent properties**:
   ```bash
   ./build/tools/sysprop set persist.wifi.ssid "MyNetwork"
   # Verify file in persistent dir
   # Re-run sysprop-init, verify property reloads
   ```
5. **Benchmarks**: `./build/benchmarks/sysprop_benchmark`
