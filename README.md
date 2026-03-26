# sysprop

A system property store for Linux embedded systems, modelled after Android's
`getprop`/`setprop`. It provides a global, process-shared key-value store with
support for volatile, read-only (`ro.*`), and persistent (`persist.*`) property
classes.

---

## Background

Embedded Linux systems often need a lightweight, shared configuration
namespace: board identifiers, runtime flags, network parameters, and similar
values that multiple processes must read without IPC overhead.

Android solves this with a memory-mapped shared region protected by seqlocks.
Generic Linux has no equivalent. Common substitutes — environment variables,
D-Bus properties, Redis, SQLite — are either process-local, require a daemon,
or carry far more overhead than a simple embedded system warrants.

sysprop takes a middle path: **one file per property**, stored in a tmpfs
directory such as `/run/`. Reads are three syscalls (`open`/`read`/`close`).
Writes are atomic via POSIX `rename(2)` on the same filesystem. No daemon, no
shared memory segment, no external dependencies at runtime.

### Property classes

| Key prefix | Behaviour |
|---|---|
| (none) | Volatile — exists in the runtime tmpfs only; lost on reboot. |
| `ro.*` | Read-only — can be set exactly once; subsequent sets are rejected. |
| `persist.*` | Persistent — written to both the runtime tmpfs and a persistent directory on disk; reloaded on next boot by `sysprop-init`. |

### Key format

Keys use the character set `[a-zA-Z0-9._-]`, dot-separated with no empty
segments. Maximum length is 256 bytes (configurable at build time).

---

## Prerequisites

| Requirement | Version | Notes |
|---|---|---|
| CMake | ≥ 3.16 | |
| C++ compiler | C++17 | GCC ≥ 8, Clang ≥ 5 recommended |
| C compiler | C11 | Required only for the C example |
| POSIX | — | `open`, `rename`, `readdir`, `mkdtemp` |
| Google Test + Mock | 1.14 | Fetched automatically if not found |
| Google Benchmark | 1.8.3 | Fetched automatically if not found |

Tests and benchmarks are optional. A minimal library+tools build has no
external runtime dependencies beyond the C++ standard library.

---

## Building

```sh
cmake -B build
cmake --build build
```

By default only the library and CLI tools are built. To enable optional components:

```sh
cmake -B build -DSYSPROP_BUILD_TESTS=ON -DSYSPROP_BUILD_BENCHMARKS=ON
cmake --build build
```

### Build options

| Option | Default | Description |
|---|---|---|
| `SYSPROP_BUILD_TOOLS` | `ON` | Build `sysprop` and `sysprop-init` CLI tools |
| `SYSPROP_BUILD_TESTS` | `OFF` | Build and register unit tests |
| `SYSPROP_BUILD_BENCHMARKS` | `OFF` | Build Google Benchmark suite |
| `SYSPROP_BUILD_EXAMPLES` | `OFF` | Build C and C++ usage examples |

### Configurable defaults

These values are baked into the library at compile time. Override them on the
CMake command line if your target uses different paths.

| Cache variable | Default | Description |
|---|---|---|
| `SYSPROP_RUNTIME_DIR` | `/run/sysprop/props` | Where volatile properties are stored |
| `SYSPROP_PERSISTENT_DIR` | `/etc/sysprop/persistent` | Where persistent properties are stored on disk |
| `SYSPROP_MAX_KEY_LENGTH` | `256` | Buffer size including null terminator; max key string is 255 bytes |
| `SYSPROP_MAX_VALUE_LENGTH` | `256` | Buffer size including null terminator; max value string is 255 bytes |

Example for a custom target:

```sh
cmake -B build \
    -DSYSPROP_RUNTIME_DIR="/run/myapp/props" \
    -DSYSPROP_PERSISTENT_DIR="/data/myapp/props"
```

### Running tests

```sh
cd build && ctest --output-on-failure
```

### Running benchmarks

```sh
./build/benchmarks/sysprop_benchmark
```

---

## Installation

```sh
cmake --install build --prefix /usr/local
```

This installs:

- `${prefix}/bin/sysprop` — busybox-style CLI (`sysprop get`, `sysprop set`, `sysprop delete`, `sysprop list`)
- `${prefix}/bin/sysprop-init` — boot-time property loader
- `${prefix}/bin/getprop` → symlink to `sysprop`
- `${prefix}/bin/setprop` → symlink to `sysprop`

Use `DESTDIR` for staged installs (e.g. when building a sysroot image):

```sh
cmake --install build --prefix /usr DESTDIR=/path/to/staging
```

### Boot-time setup

Add `sysprop-init` to your init sequence (before any service that reads
properties). A minimal invocation:

```sh
# Creates the runtime directory and loads any persist.* properties from disk.
sysprop-init

# Optionally load a defaults file (key=value lines; # introduces a comment).
sysprop-init /etc/sysprop/defaults.prop
```

`sysprop-init` also removes stale `.tmp.*` files left by writers that crashed
mid-write, preventing stale data from surviving across reboots.

### CLI usage

```sh
# Get a property (prints empty line if missing, like Android getprop)
getprop ro.build.version
sysprop get ro.build.version

# Get with a fallback default
getprop missing.key "fallback-value"
sysprop get missing.key "fallback-value"

# Set a property
setprop device.name my-board
sysprop set device.name my-board

# Delete a property
sysprop delete device.name

# List all properties in [key]: [value] format
getprop
sysprop list
```

Runtime and persistent directories can be overridden per-invocation with
environment variables:

```sh
SYSPROP_RUNTIME_DIR=/tmp/test sysprop get device.name
```

---

## Incorporating into an existing project

### Option A — CMake FetchContent (recommended)

Add to your `CMakeLists.txt`:

```cmake
include(FetchContent)
FetchContent_Declare(
    sysprop
    GIT_REPOSITORY https://github.com/yourorg/sysprop.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(sysprop)

# Link your target against the library
target_link_libraries(my_app PRIVATE sysprop)
```

Tests, benchmarks, tools, and examples all default to `OFF`, so no additional
options are needed when consuming the library as a dependency. The library's
public include directory (`include/`) is propagated automatically via
`target_include_directories(sysprop PUBLIC ...)`, so no additional
`include_directories` call is needed.

#### Installing the CLI tools alongside your project

`SYSPROP_BUILD_TOOLS` defaults to `ON`, so the `sysprop` and `sysprop-init`
binaries are built automatically when you use FetchContent. Their install rules
are registered with your parent project, so a single `cmake --install` deploys
everything together:

```cmake
include(FetchContent)
FetchContent_Declare(
    sysprop
    GIT_REPOSITORY https://github.com/yourorg/sysprop.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
)
# SYSPROP_BUILD_TOOLS is ON by default — listed here for clarity.
set(SYSPROP_BUILD_TOOLS ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(sysprop)

target_link_libraries(my_app PRIVATE sysprop)
```

Build and install:

```sh
cmake -B build
cmake --build build
cmake --install build --prefix /usr/local
```

This installs your project's own targets **and**:

- `/usr/local/bin/sysprop`
- `/usr/local/bin/sysprop-init`
- `/usr/local/bin/getprop` → symlink to `sysprop`
- `/usr/local/bin/setprop` → symlink to `sysprop`

To include the tools in the build but exclude them from `cmake --install`,
use CMake's component mechanism or set `SYSPROP_BUILD_TOOLS=OFF` and build
them separately as shown in the standalone [Installation](#installation) section.

### Option B — manual build tree reference

The current build does not install the library or headers — `cmake --install`
only installs the CLI tools. To consume the library from a non-CMake build
system, build the project first and reference the artifacts directly:

```sh
cmake -B /tmp/sysprop-build \
    -DSYSPROP_BUILD_TESTS=OFF \
    -DSYSPROP_BUILD_BENCHMARKS=OFF \
    -DSYSPROP_BUILD_TOOLS=OFF \
    -DSYSPROP_BUILD_EXAMPLES=OFF
cmake --build /tmp/sysprop-build
```

Then point your compiler at the build output:

```sh
# C
cc -I/path/to/sysprop/include main.c \
   /tmp/sysprop-build/src/libsysprop.a -o my_app

# C++
c++ -std=c++17 -I/path/to/sysprop/include main.cpp \
   /tmp/sysprop-build/src/libsysprop.a -o my_app
```

> If you need `cmake --install` to deploy the library and headers, install
> rules for `sysprop` and `include/` would need to be added to
> `src/CMakeLists.txt` first.

### C usage

The runtime (and persistent) directories must exist before the first `Set`
call — the library does not create them. In production the `sysprop-init`
binary handles this at boot time. For application-level use, create the
directories explicitly before calling `sysprop_init`.

```c
#include <stdio.h>
#include <sys/stat.h>
#include <sysprop/sysprop.h>

int main(void) {
    /* Create runtime directory if it does not exist. */
    mkdir("/run/myapp/props", 0755);

    sysprop_config_t cfg;
    cfg.runtime_dir        = "/run/myapp/props";
    cfg.persistent_dir     = "/data/myapp/props";
    cfg.enable_persistence = 0;  /* set to 1 once persistent dir also exists */
    sysprop_init(&cfg);

    sysprop_set("device.name", "my-board");
    sysprop_set("ro.build.version", "1.0.0");

    char buf[SYSPROP_MAX_VALUE_LENGTH];   /* includes null terminator */
    if (sysprop_get("device.name", buf, sizeof(buf)) >= 0)
        printf("device.name = %s\n", buf);

    /* ro.* properties are write-once. */
    int rc = sysprop_set("ro.build.version", "2.0.0");
    printf("%s\n", sysprop_error_string(rc));  /* "read-only property" */

    return 0;
}
```

### C++ usage

The same directory pre-existence requirement applies. See `examples/cpp_example/main.cpp`
for a self-contained runnable example that uses `/tmp` paths.

```cpp
#include <array>
#include <cstdio>
#include <sys/stat.h>
#include <sysprop/sysprop.h>

int main() {
    mkdir("/run/myapp/props", 0755);

    sysprop_config_t cfg;
    cfg.runtime_dir        = "/run/myapp/props";
    cfg.persistent_dir     = "/data/myapp/props";
    cfg.enable_persistence = 0;
    sysprop_init(&cfg);

    sysprop_set("device.name", "my-board");
    sysprop_set("ro.build.version", "1.0.0");

    std::array<char, SYSPROP_MAX_VALUE_LENGTH> buf{};  // includes null terminator
    if (sysprop_get("device.name", buf.data(), buf.size()) >= 0)
        std::printf("device.name = %s\n", buf.data());

    std::printf("int    = %d\n",   sysprop_get_int("missing.int", 42));
    std::printf("bool   = %s\n",   sysprop_get_bool("missing.bool", 0) ? "true" : "false");
    std::printf("float  = %.2f\n", sysprop_get_float("missing.float", 3.14f));

    int rc = sysprop_set("ro.build.version", "2.0.0");
    std::printf("%s\n", sysprop_error_string(rc));  // "read-only property"
}
```

### Error handling

All functions return an `int`. Zero (`SYSPROP_OK`) is success; negative values
are errors. Callers may check or ignore return values as appropriate.

```c
int rc = sysprop_set("ro.x", "v");
if (rc != SYSPROP_OK)
    fprintf(stderr, "set failed: %s\n", sysprop_error_string(rc));
```

---

## Project structure

```
sysprop/
├── include/sysprop/
│   └── sysprop.h          # Single public header (C and C++)
├── src/
│   ├── backend.h          # Abstract storage backend (internal)
│   ├── file_backend.h/.cpp
│   ├── property_store.h/.cpp
│   ├── validation.h/.cpp
│   └── sysprop.cpp        # Public API implementation
├── tools/
│   ├── sysprop_main.cpp   # Busybox-style CLI (getprop / setprop / sysprop)
│   └── sysprop_init.cpp   # Boot-time property loader
├── tests/
│   ├── validation_test.cpp
│   ├── file_backend_test.cpp
│   ├── property_store_test.cpp
│   └── integration_test.cpp
├── benchmarks/
│   └── sysprop_benchmark.cpp
└── examples/
    ├── c_example/main.c
    └── cpp_example/main.cpp
```
