#pragma once

#include <cstddef>
#include <functional>

namespace sysprop::internal {

// Abstract interface for a property store.
//
// Concrete implementations:
//   FilePropertyStore  — policy-enforcing store backed by the filesystem
//   MockPropertyStore  — in-memory hash-map store for testing (RAII injection)
class PropertyStore {
 public:
  PropertyStore() = default;
  PropertyStore(const PropertyStore&) = delete;
  PropertyStore& operator=(const PropertyStore&) = delete;
  PropertyStore(PropertyStore&&) = delete;
  PropertyStore& operator=(PropertyStore&&) = delete;
  virtual ~PropertyStore() = default;

  [[nodiscard]] virtual int Get(const char* key, char* buf, std::size_t buf_len) = 0;

  // Regular write. ro.* keys are always rejected with SYSPROP_ERR_READ_ONLY.
  [[nodiscard]] virtual int Set(const char* key, const char* value) = 0;

  // Privileged write used ONLY by sysprop-init (via LoadDefaultsFile).
  // Allows ro.* keys to be written exactly once at boot; subsequent calls for
  // the same ro.* key return SYSPROP_ERR_READ_ONLY. For non-ro.* keys,
  // behaviour is identical to Set().
  //
  // DO NOT call this from application code. The only permitted caller is
  // LoadDefaultsFile() in tools/defaults_loader.cpp.
  [[nodiscard]] virtual int SetInit(const char* key, const char* value) = 0;

  [[nodiscard]] virtual int Delete(const char* key) = 0;
  [[nodiscard]] virtual int Exists(const char* key) = 0;

  // Calls visitor(key, value) for every property in the store.
  // Not a hot path — callers may heap-allocate within visitor.
  [[nodiscard]] virtual int ForEach(const std::function<void(const char*, const char*)>& visitor) = 0;
};

}  // namespace sysprop::internal
