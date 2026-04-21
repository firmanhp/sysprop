#pragma once  // NOLINT(llvm-header-guard) -- #pragma once is used throughout; llvm-header-guard
              // requires #ifndef-style guards

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
  using Visitor = std::function<bool(const char* key, const char* value)>;

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

  // Iterate over all properties. Visitor returns false to stop early.
  [[nodiscard]] virtual int ForEach(Visitor fn) = 0;

  // Load non-persist keys from the persistent backend into runtime (e.g., factory ro.*).
  // No-op if no persistent backend. Returns count of loaded properties.
  [[nodiscard]] virtual int LoadPersistentProperties() = 0;
};

}  // namespace sysprop::internal
