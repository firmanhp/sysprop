// sysprop — System property store: C and C++ interface.
//
// A single header usable from both C and C++.
//
// Usage:
//   #include <sysprop/sysprop.h>
//
//   sysprop_set("device.name", "my-board");
//   char buf[SYSPROP_MAX_VALUE_LENGTH];   // includes null terminator
//   sysprop_get("device.name", buf, sizeof(buf));

#pragma once

#ifdef __cplusplus
#  include <cstddef>
#else
#  include <stddef.h>
#endif

// ── Configurable compile-time defaults ────────────────────────────────────────
// Override by passing -DSYSPROP_RUNTIME_DIR="..." at compile time or via the
// CMake cache variable of the same name.

#ifndef SYSPROP_RUNTIME_DIR
#  define SYSPROP_RUNTIME_DIR "/run/sysprop/props"
#endif
#ifndef SYSPROP_PERSISTENT_DIR
#  define SYSPROP_PERSISTENT_DIR "/etc/sysprop/persistent"
#endif
// Buffer sizes including the null terminator.
// Max string length is (SYSPROP_MAX_KEY_LENGTH - 1) and (SYSPROP_MAX_VALUE_LENGTH - 1).
#ifndef SYSPROP_MAX_KEY_LENGTH
#  define SYSPROP_MAX_KEY_LENGTH 256
#endif
#ifndef SYSPROP_MAX_VALUE_LENGTH
#  define SYSPROP_MAX_VALUE_LENGTH 256
#endif

// ── Error codes ───────────────────────────────────────────────────────────────
// Negative = error; 0 = success. Functions returning a byte count return a
// positive value on success.

#define SYSPROP_OK                    0
#define SYSPROP_ERR_NOT_FOUND        (-1)
#define SYSPROP_ERR_READ_ONLY        (-2)
#define SYSPROP_ERR_INVALID_KEY      (-3)
#define SYSPROP_ERR_VALUE_TOO_LONG   (-4)
#define SYSPROP_ERR_KEY_TOO_LONG     (-5)
#define SYSPROP_ERR_IO               (-6)
#define SYSPROP_ERR_PERMISSION       (-7)
#define SYSPROP_ERR_NOT_INITIALIZED  (-8)
#define SYSPROP_ERR_BUFFER_TOO_SMALL (-9)

// ── Config ────────────────────────────────────────────────────────────────────

typedef struct sysprop_config {
  const char* runtime_dir;     /* NULL → SYSPROP_RUNTIME_DIR    */
  const char* persistent_dir;  /* NULL → SYSPROP_PERSISTENT_DIR */
  int         enable_persistence; /* 0 = disabled, non-zero = enabled */
} sysprop_config_t;

// ── API ───────────────────────────────────────────────────────────────────────

#ifdef __cplusplus
extern "C" {
#endif

// Returns a human-readable string for an error code.
const char* sysprop_error_string(int err);

// Initialize the library with the given config.
// Pass NULL to use compiled-in defaults.
// Safe to call multiple times; only the first call takes effect.
// Returns SYSPROP_OK or a negative error code.
int sysprop_init(const sysprop_config_t* config);

// Read the value of key into the caller-provided buffer.
// buf is always null-terminated on success when buf_len > 0.
// Returns bytes written (excluding null terminator), or a negative error code.
int sysprop_get(const char* key, char* buf, size_t buf_len);

// Set key=value. Returns SYSPROP_OK or a negative error code.
int sysprop_set(const char* key, const char* value);

// Delete key. Returns SYSPROP_ERR_NOT_FOUND if absent.
int sysprop_delete(const char* key);

// Typed helpers. Return default_value if the property is absent or unparseable.
int   sysprop_get_int(const char* key, int default_value);
int   sysprop_get_bool(const char* key, int default_value);  /* returns 0 or 1 */
float sysprop_get_float(const char* key, float default_value);

#ifdef __cplusplus
}  // extern "C"
#endif

