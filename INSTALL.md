# Installing and integrating sysprop

## Prerequisites

| Requirement | Notes |
|---|---|
| CMake ≥ 3.16 | |
| C++20 compiler | GCC ≥ 10 or Clang ≥ 10 |
| POSIX filesystem | `open`, `rename`, `readdir` |
| Google Test 1.14 | Fetched automatically when `SYSPROP_BUILD_TESTS=ON` |
| Google Benchmark 1.8.3 | Fetched automatically when `SYSPROP_BUILD_BENCHMARKS=ON` |

A library-only build has no runtime dependencies beyond the C++ standard library.

---

## Building

```sh
cmake -B build
cmake --build build
```

Optional components are disabled by default:

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

### Configurable paths and limits

These values are baked into the library at compile time.

| Cache variable | Default | Description |
|---|---|---|
| `SYSPROP_RUNTIME_DIR` | `/run/sysprop/props` | Where volatile and `ro.*` properties are stored |
| `SYSPROP_PERSISTENT_DIR` | `/etc/sysprop/persistent` | Where `persist.*` properties are stored on disk |
| `SYSPROP_MAX_KEY_LENGTH` | `256` | Buffer size including null terminator |
| `SYSPROP_MAX_VALUE_LENGTH` | `256` | Buffer size including null terminator |

Set `SYSPROP_PERSISTENT_DIR` to an empty string to disable persistence entirely.
When disabled, `persist.*` operations fall back to the runtime backend.

```sh
cmake -B build \
    -DSYSPROP_RUNTIME_DIR="/run/myapp/props" \
    -DSYSPROP_PERSISTENT_DIR="/data/myapp/props"
```

### Running tests

```sh
cmake -B build -DSYSPROP_BUILD_TESTS=ON
cmake --build build
cd build && ctest --output-on-failure
```

---

## Installation

```sh
cmake --install build --prefix /usr/local
```

Installs:

- `${prefix}/bin/sysprop`
- `${prefix}/bin/sysprop-init`
- `${prefix}/bin/getprop` → `sysprop`
- `${prefix}/bin/setprop` → `sysprop`

Use `DESTDIR` for staged installs:

```sh
cmake --install build --prefix /usr DESTDIR=/path/to/staging
```

`cmake --install` installs only the CLI tools. The library (`libsysprop.a`) and
headers are not installed — consume them directly from the build tree via
FetchContent or a manual build (see below).

---

## CMake integration

### FetchContent (recommended)

```cmake
include(FetchContent)
FetchContent_Declare(
    sysprop
    GIT_REPOSITORY https://github.com/yourorg/sysprop.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(sysprop)

target_link_libraries(my_app PRIVATE sysprop)
```

Tests, benchmarks, and examples default to `OFF`. `SYSPROP_BUILD_TOOLS`
defaults to `ON`; suppress it if you don't need the CLI tools:

```cmake
set(SYSPROP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(sysprop)
```

### Manual build tree

```sh
cmake -B /tmp/sysprop-build \
    -DSYSPROP_BUILD_TOOLS=OFF \
    -DSYSPROP_BUILD_TESTS=OFF
cmake --build /tmp/sysprop-build
```

```sh
# C
cc -I/path/to/sysprop/include main.c \
   /tmp/sysprop-build/src/libsysprop.a -o my_app

# C++
c++ -std=c++20 -I/path/to/sysprop/include main.cpp \
   /tmp/sysprop-build/src/libsysprop.a -o my_app
```

---

## Library API

The library auto-initializes before `main()` runs using the paths baked in at
build time. No explicit init call is needed.

```c
#include <sysprop/sysprop.h>
```

### Core functions

```c
// Read a property into the caller-provided buffer.
// Returns bytes written (>= 0) on success, negative error code on failure.
int sysprop_get(const char* key, char* buf, size_t buf_len);

// Set a property. Returns SYSPROP_OK (0) or a negative error code.
int sysprop_set(const char* key, const char* value);

// Delete a property. Returns SYSPROP_ERR_NOT_FOUND if absent.
int sysprop_delete(const char* key);

// Translate an error code to a human-readable string.
const char* sysprop_error_string(int err);
```

### Typed helpers

```c
// Return default_value if the property is absent or unparseable.
int64_t sysprop_get_int(const char* key, int64_t default_value);
int     sysprop_get_bool(const char* key, int default_value);  /* returns 0 or 1 */
float   sysprop_get_float(const char* key, float default_value);
```

`sysprop_get_bool` recognises `"1"`, `"true"`, `"yes"`, `"on"` as true and
`"0"`, `"false"`, `"no"`, `"off"` as false.

### Error handling

Zero (`SYSPROP_OK`) is success; negative values are errors. `sysprop_get`
returns the byte count (≥ 0) on success, so callers must check `>= 0`, not
`== SYSPROP_OK`.

```c
int rc = sysprop_set("ro.x", "v");
if (rc != SYSPROP_OK)
    fprintf(stderr, "set failed: %s\n", sysprop_error_string(rc));
```

### C example

```c
#include <stdio.h>
#include <sysprop/sysprop.h>

int main(void) {
    sysprop_set("device.name", "my-board");

    char buf[SYSPROP_MAX_VALUE_LENGTH];
    if (sysprop_get("device.name", buf, sizeof(buf)) >= 0)
        printf("device.name = %s\n", buf);

    /* ro.* properties are set only by sysprop-init; sysprop_set() rejects them. */
    int rc = sysprop_set("ro.build.version", "1.0.0");
    printf("%s\n", sysprop_error_string(rc));  /* "read-only property" */

    return 0;
}
```

### C++ example

```cpp
#include <array>
#include <cstdio>
#include <string>
#include <sysprop/sysprop.h>

int main() {
    sysprop_set("device.name", "my-board");

    std::array<char, SYSPROP_MAX_VALUE_LENGTH> buf{};
    if (sysprop_get("device.name", buf.data(), buf.size()) >= 0)
        std::printf("device.name = %s\n", buf.data());

    int64_t n = sysprop_get_int("missing.int", 42);
    int     b = sysprop_get_bool("missing.bool", 0);   /* 0 or 1 */
    float   f = sysprop_get_float("missing.float", 3.14f);

    /* C++ string overload (defined in the header). */
    std::string v = sysprop_get("device.name", std::string{"unknown"});
}
```
