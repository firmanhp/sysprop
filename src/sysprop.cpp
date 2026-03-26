#include <sysprop/sysprop.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string_view>

#include "file_backend.h"
#include "property_store.h"

namespace {

using sysprop::internal::FileBackend;
using sysprop::internal::PropertyStore;

// ── Global singleton ──────────────────────────────────────────────────────────
//
// System properties are global state by definition (analogous to environment
// variables). The function-local static is initialized exactly once on first
// use; C++11 guarantees this is thread-safe without any explicit locking.
//
// The first call to get() wins: subsequent calls with a different config are
// silently ignored, matching the behaviour of the previous call_once approach.

class GlobalStore {
 public:
  static PropertyStore& get(const sysprop_config_t* cfg = nullptr) {
    static GlobalStore instance{cfg};
    return instance.store_;
  }

 private:
  explicit GlobalStore(const sysprop_config_t* cfg)
      : runtime_{(cfg && cfg->runtime_dir) ? cfg->runtime_dir : SYSPROP_RUNTIME_DIR},
        persistent_{((cfg == nullptr) || (cfg->enable_persistence != 0))
                        ? std::make_unique<FileBackend>(
                              (cfg && cfg->persistent_dir) ? cfg->persistent_dir
                                                           : SYSPROP_PERSISTENT_DIR)
                        : nullptr},
        store_{&runtime_, persistent_.get()} {}

  FileBackend                  runtime_;
  std::unique_ptr<FileBackend> persistent_;
  PropertyStore                store_;
};

}  // namespace

// ── Public symbols (C linkage) ────────────────────────────────────────────────

extern "C" {

const char* sysprop_error_string(int err) {
  switch (err) {
    case SYSPROP_OK:                   return "ok";
    case SYSPROP_ERR_NOT_FOUND:        return "not found";
    case SYSPROP_ERR_READ_ONLY:        return "read-only property";
    case SYSPROP_ERR_INVALID_KEY:      return "invalid key";
    case SYSPROP_ERR_VALUE_TOO_LONG:   return "value too long";
    case SYSPROP_ERR_KEY_TOO_LONG:     return "key too long";
    case SYSPROP_ERR_IO:               return "I/O error";
    case SYSPROP_ERR_PERMISSION:       return "permission denied";
    case SYSPROP_ERR_NOT_INITIALIZED:  return "not initialized";
    case SYSPROP_ERR_BUFFER_TOO_SMALL: return "buffer too small";
    default:                           return "unknown error";
  }
}

int sysprop_init(const sysprop_config_t* config) {
  GlobalStore::get(config);
  return SYSPROP_OK;
}

int sysprop_get(const char* key, char* buf, size_t buf_len) {
  return GlobalStore::get().Get(key, buf, buf_len);
}

int sysprop_set(const char* key, const char* value) {
  return GlobalStore::get().Set(key, value);
}

int sysprop_delete(const char* key) { return GlobalStore::get().Delete(key); }

int sysprop_get_int(const char* key, int default_value) {
  char buf[SYSPROP_MAX_VALUE_LENGTH];
  if (GlobalStore::get().Get(key, buf, sizeof(buf)) < 0) return default_value;

  char* end = nullptr;
  errno = 0;
  const long val = std::strtol(buf, &end, 10);
  if (end == buf || errno != 0) return default_value;
  return static_cast<int>(val);
}

int sysprop_get_bool(const char* key, int default_value) {
  char buf[SYSPROP_MAX_VALUE_LENGTH];
  if (GlobalStore::get().Get(key, buf, sizeof(buf)) < 0) return default_value ? 1 : 0;

  const std::string_view sv{buf};
  if (sv == "1" || sv == "true" || sv == "yes" || sv == "on") return 1;
  if (sv == "0" || sv == "false" || sv == "no" || sv == "off") return 0;
  return default_value ? 1 : 0;
}

float sysprop_get_float(const char* key, float default_value) {
  char buf[SYSPROP_MAX_VALUE_LENGTH];
  if (GlobalStore::get().Get(key, buf, sizeof(buf)) < 0) return default_value;

  char* end = nullptr;
  errno = 0;
  const float val = std::strtof(buf, &end);
  if (end == buf || errno != 0) return default_value;
  return val;
}

}  // extern "C"
