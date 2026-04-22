#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <string>
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
// All configuration is baked in at compile time (SYSPROP_RUNTIME_DIR etc.).
// constexpr constructors throughout the chain let the linker constant-initialize
// g_instance before any dynamic initialization runs — no runtime work, no
// placement-new, no __attribute__((constructor)).
//
// Thread safety: constant initialization completes before any dynamic init or
// thread creation. Concurrent property access is safe because FileBackend
// operations are individually atomic (rename(2)).

struct GlobalStore {
  constexpr GlobalStore()
      : runtime_{SYSPROP_RUNTIME_DIR},
        persistent_{SYSPROP_ENABLE_PERSISTENCE ? SYSPROP_PERSISTENT_DIR : SYSPROP_RUNTIME_DIR},
        store_{runtime_, persistent_} {}

  FileBackend runtime_;
  FileBackend persistent_;
  FilePropertyStore store_;
};

GlobalStore g_instance;
PropertyStore* s_store_override = nullptr;

PropertyStore* GetStore() {
  if (s_store_override != nullptr) { return s_store_override; }
  return &g_instance.store_;
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

std::string sysprop_dump() {
  std::map<std::string, std::string> props;
  // On I/O error, ForEach returns a negative code and props is partial or empty.
  // sysprop_dump() returns whatever was collected; callers that need to distinguish
  // I/O failure from an empty store should use PropertyStore::ForEach directly.
  (void)GetStore()->ForEach([&props](const char* key, const char* value) {
    props[key] = value;
  });
  std::string out;
  for (const auto& [key, value] : props) {
    out += '[';
    out += key;
    out += "]: [";
    out += value;
    out += "]\n";
  }
  return out;
}

namespace sysprop::testing {

sysprop::internal::PropertyStore* swap_store(sysprop::internal::PropertyStore* new_store) {
  PropertyStore* prev = s_store_override;
  s_store_override = new_store;
  return prev;
}

}  // namespace sysprop::testing
