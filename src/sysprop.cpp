#include <sysprop/sysprop.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <new> // NOLINT(misc-include-cleaner) -- required for placement new (new (s_storage) ...) even though it arrives transitively
#include <optional>
#include <string_view>

#include "file_backend.h"
#include "property_store.h"

namespace {

using sysprop::internal::FileBackend;
using sysprop::internal::PropertyStore;

// ── Global singleton ──────────────────────────────────────────────────────────
//
// Constructed once by sysprop_init() into static storage (no heap). Until
// sysprop_init() is called, s_instance is null and all API calls return
// SYSPROP_ERR_NOT_INITIALIZED.
//
// Thread safety: sysprop_init() is expected to be called from main() before
// any threads are started. Concurrent property access after init is safe
// because FileBackend operations are individually atomic (rename(2)).

struct GlobalStore {
  explicit GlobalStore(const sysprop_config_t* cfg)
      : runtime_{(cfg && cfg->runtime_dir) ? cfg->runtime_dir : SYSPROP_RUNTIME_DIR},
        persistent_{MakePersistent(cfg)},
        store_{&runtime_, persistent_ ? &*persistent_ : nullptr} {}

  static std::optional<FileBackend> MakePersistent(const sysprop_config_t* cfg) {
    if ((cfg == nullptr) || (cfg->enable_persistence != 0)) {
      return FileBackend{(cfg && cfg->persistent_dir) ? cfg->persistent_dir
                                                      : SYSPROP_PERSISTENT_DIR};
    }
    return std::nullopt;
  }

  FileBackend                runtime_;   // NOLINT(misc-non-private-member-variables-in-classes) -- GlobalStore is an internal impl struct, not a public API class
  std::optional<FileBackend> persistent_; // NOLINT(misc-non-private-member-variables-in-classes)
  PropertyStore              store_;      // NOLINT(misc-non-private-member-variables-in-classes)
};

// Raw storage avoids any static-initializer or atexit registration.
// Placement-new'd by sysprop_init(); never explicitly destructed (lifetime
// matches the process — the OS reclaims on exit).
alignas(GlobalStore) static unsigned char s_storage[sizeof(GlobalStore)]; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables,readability-static-definition-in-anonymous-namespace) -- placement-new singleton buffer; intentionally non-const and file-scoped
static GlobalStore* s_instance = nullptr; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables,readability-static-definition-in-anonymous-namespace) -- singleton pointer; null until sysprop_init()

PropertyStore* GetStore() {
  return s_instance ? &s_instance->store_ : nullptr;
}

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
  if (s_instance) { return SYSPROP_OK; }  // already initialized; ignore
  s_instance = new (s_storage) GlobalStore{config}; // NOLINT(cppcoreguidelines-owning-memory) -- placement new into static storage; no heap allocation, ownership concept does not apply
  return SYSPROP_OK;
}

int sysprop_get(const char* key, char* buf, size_t buf_len) {
  PropertyStore* s = GetStore();
  if (!s) { return SYSPROP_ERR_NOT_INITIALIZED; }
  return s->Get(key, buf, buf_len);
}

int sysprop_set(const char* key, const char* value) {
  PropertyStore* s = GetStore();
  if (!s) { return SYSPROP_ERR_NOT_INITIALIZED; }
  return s->Set(key, value);
}

int sysprop_delete(const char* key) {
  PropertyStore* s = GetStore();
  if (!s) { return SYSPROP_ERR_NOT_INITIALIZED; }
  return s->Delete(key);
}

int sysprop_get_int(const char* key, int default_value) {
  char buf[SYSPROP_MAX_VALUE_LENGTH];
  PropertyStore* s = GetStore();
  if (!s || s->Get(key, buf, sizeof(buf)) < 0) { return default_value; }

  char* end = nullptr;
  errno = 0;
  const long val = std::strtol(buf, &end, 10); // NOLINT(google-runtime-int) -- strtol returns long by specification; narrowing to int64_t would misrepresent the API
  if (end == buf || errno != 0) { return default_value; }
  return static_cast<int>(val);
}

int sysprop_get_bool(const char* key, int default_value) {
  char buf[SYSPROP_MAX_VALUE_LENGTH];
  PropertyStore* s = GetStore();
  if (!s || s->Get(key, buf, sizeof(buf)) < 0) { return default_value ? 1 : 0; }

  const std::string_view sv{buf};
  if (sv == "1" || sv == "true" || sv == "yes" || sv == "on") { return 1; }
  if (sv == "0" || sv == "false" || sv == "no" || sv == "off") { return 0; }
  return default_value ? 1 : 0;
}

float sysprop_get_float(const char* key, float default_value) {
  char buf[SYSPROP_MAX_VALUE_LENGTH];
  PropertyStore* s = GetStore();
  if (!s || s->Get(key, buf, sizeof(buf)) < 0) { return default_value; }

  char* end = nullptr;
  errno = 0;
  const float val = std::strtof(buf, &end);
  if (end == buf || errno != 0) { return default_value; }
  return val;
}

}  // extern "C"
