#pragma once

#include <cstddef>

namespace sysprop::internal {

// Abstract interface for a property store.
//
// Concrete implementations:
//   FilePropertyStore  — policy-enforcing store backed by the filesystem
//   MockPropertyStore  — in-memory hash-map store for testing (RAII injection)
class PropertyStore {
 public:
  PropertyStore() = default;
  PropertyStore(const PropertyStore&) = default;
  PropertyStore& operator=(const PropertyStore&) = default;
  PropertyStore(PropertyStore&&) = default;
  PropertyStore& operator=(PropertyStore&&) = default;
  virtual ~PropertyStore() = default;

  [[nodiscard]] virtual int Get(const char* key, char* buf, std::size_t buf_len) = 0;
  [[nodiscard]] virtual int Set(const char* key, const char* value) = 0;
  [[nodiscard]] virtual int Delete(const char* key) = 0;
  [[nodiscard]] virtual int Exists(const char* key) = 0;
};

}  // namespace sysprop::internal
