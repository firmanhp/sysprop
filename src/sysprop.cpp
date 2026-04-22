#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>  // NOLINT(misc-include-cleaner) -- required for placement new (new (s_storage) ...) even though it arrives transitively
#include <optional>
#include <string_view>

#include <sysprop/sysprop.h>

#include "file_backend.h"
#include "file_property_store.h"
#include <sysprop/property_store.h>
#include <sysprop/testing/internal.h>

namespace {

using sysprop::internal::FileBackend;
using sysprop::internal::FilePropertyStore;
using sysprop::internal::PropertyStore;

// ── Global singleton ──────────────────────────────────────────────────────────
//
// Constructed once via __attribute__((constructor)) before main() runs.
// Directories and persistence are baked in at compile time via CMake.
// GetStore() returns nullptr only when both s_store_override and s_instance
// are null — impossible after sysprop_auto_init() runs. Call sites dereference
// the result directly; the null branch is unreachable in practice.
//
// Thread safety: the constructor attribute runs before any threads are started.
// Concurrent property access after init is safe because FileBackend operations
// are individually atomic (rename(2)).

struct GlobalStore {
  GlobalStore()
      : runtime_{SYSPROP_RUNTIME_DIR},
        persistent_{MakePersistent()},
        store_{&runtime_, persistent_ ? &*persistent_ : nullptr} {}

  static std::optional<FileBackend> MakePersistent() {
#if SYSPROP_ENABLE_PERSISTENCE
    return FileBackend{SYSPROP_PERSISTENT_DIR};
#else
    return std::nullopt;
#endif
  }

  FileBackend runtime_;
  std::optional<FileBackend> persistent_;
  FilePropertyStore store_;
};

// Raw storage avoids any static-initializer or atexit registration.
// Placement-new'd by sysprop_auto_init(); never explicitly destructed (lifetime
// matches the process — the OS reclaims on exit).
alignas(GlobalStore) unsigned char s_storage[sizeof(GlobalStore)];
GlobalStore* s_instance = nullptr;
PropertyStore* s_store_override = nullptr;

PropertyStore* GetStore() {
  if (s_store_override != nullptr) { return s_store_override; }
  return s_instance ? &s_instance->store_ : nullptr;
}

__attribute__((constructor))
void sysprop_auto_init() {
  if (s_instance) { return; }  // guard: safe if ever used as a shared library
  s_instance = new (s_storage) GlobalStore{};  // NOLINT(cppcoreguidelines-owning-memory) -- placement new into static storage; no heap allocation
}

}  // namespace

// ── Public symbols (C linkage) ────────────────────────────────────────────────

extern "C" {

const char* sysprop_error_string(int err) {
  switch (err) {
    case SYSPROP_OK:
      return "ok";
    case SYSPROP_ERR_NOT_FOUND:
      return "not found";
    case SYSPROP_ERR_READ_ONLY:
      return "read-only property";
    case SYSPROP_ERR_INVALID_KEY:
      return "invalid key";
    case SYSPROP_ERR_VALUE_TOO_LONG:
      return "value too long";
    case SYSPROP_ERR_KEY_TOO_LONG:
      return "key too long";
    case SYSPROP_ERR_IO:
      return "I/O error";
    case SYSPROP_ERR_PERMISSION:
      return "permission denied";
    case SYSPROP_ERR_INVALID_VALUE:
      return "invalid value";
    case SYSPROP_ERR_BUFFER_TOO_SMALL:
      return "buffer too small";
    default:
      return "unknown error";
  }
}

int sysprop_get(const char* key, char* buf, size_t buf_len) {
  return GetStore()->Get(key, buf, buf_len);
}

int sysprop_set(const char* key, const char* value) {
  return GetStore()->Set(key, value);
}

int sysprop_delete(const char* key) {
  return GetStore()->Delete(key);
}

int64_t sysprop_get_int(const char* key, int64_t default_value) {
  char buf[SYSPROP_MAX_VALUE_LENGTH];
  if (GetStore()->Get(key, buf, sizeof(buf)) < 0) {
    return default_value;
  }

  char* end = nullptr;
  errno = 0;
  const int64_t val = std::strtol(buf, &end, 10);
  if (end == buf || errno != 0) {
    return default_value;
  }
  return val;
}

int sysprop_get_bool(const char* key, int default_value) {
  char buf[SYSPROP_MAX_VALUE_LENGTH];
  if (GetStore()->Get(key, buf, sizeof(buf)) < 0) {
    return default_value ? 1 : 0;
  }

  const std::string_view sv{buf};
  if (sv == "1" || sv == "true" || sv == "yes" || sv == "on") {
    return 1;
  }
  if (sv == "0" || sv == "false" || sv == "no" || sv == "off") {
    return 0;
  }
  return default_value ? 1 : 0;
}

float sysprop_get_float(const char* key, float default_value) {
  char buf[SYSPROP_MAX_VALUE_LENGTH];
  if (GetStore()->Get(key, buf, sizeof(buf)) < 0) {
    return default_value;
  }

  char* end = nullptr;
  errno = 0;
  const float val = std::strtof(buf, &end);
  if (end == buf || errno != 0) {
    return default_value;
  }
  return val;
}

}  // extern "C"

namespace sysprop::testing {

sysprop::internal::PropertyStore* swap_store(sysprop::internal::PropertyStore* new_store) {
  PropertyStore* prev = s_store_override;
  s_store_override = new_store;
  return prev;
}

}  // namespace sysprop::testing
