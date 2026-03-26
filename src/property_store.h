#pragma once

#include <cstddef>

#include "backend.h"

namespace sysprop::internal {

// PropertyStore enforces property policy on top of raw storage backends.
//
// Policy rules:
//   - ro.*     : can be set exactly once; subsequent sets return ERR_READ_ONLY.
//   - persist.*: on set, written to both the runtime and persistent backends.
//               LoadPersistentProperties() copies values from the persistent
//               backend into the runtime backend (called at boot time).
//   - Other    : volatile — only written to the runtime backend.
//
// All key and value inputs are validated before any backend operation.
class PropertyStore {
 public:
  // runtime_backend is required. persistent_backend may be null (disables
  // persistence). Both pointers must outlive this object.
  PropertyStore(Backend* runtime_backend, Backend* persistent_backend);

  [[nodiscard]] int Get(const char* key, char* buf, std::size_t buf_len);
  [[nodiscard]] int Set(const char* key, const char* value);
  [[nodiscard]] int Delete(const char* key);
  [[nodiscard]] int Exists(const char* key);

  // Iterate over all runtime properties.
  [[nodiscard]] int ForEach(Backend::Visitor visitor);

  // Load persisted properties from persistent_backend into runtime_backend.
  // No-op if persistent_backend is null. Returns the number of loaded
  // properties on success, or a negative Error code.
  [[nodiscard]] int LoadPersistentProperties();

 private:
  Backend* runtime_;
  Backend* persistent_;  // may be null
};

}  // namespace sysprop::internal
