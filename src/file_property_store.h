#pragma once

#include <cstddef>

#include "file_backend.h"
#include <sysprop/property_store.h>

namespace sysprop::internal {

// Policy-enforcing PropertyStore backed by two FileBackend instances.
//
// Policy rules:
//   - ro.*     : can be set exactly once; subsequent sets return ERR_READ_ONLY.
//   - persist.*: all operations go directly to the persistent backend (no
//               runtime copy). Falls back to runtime if persistent is null.
//   - Other    : volatile — only written to the runtime backend.
//
// All key and value inputs are validated before any backend operation.
class FilePropertyStore final : public PropertyStore {
 public:
  // runtime_backend is required. persistent_backend may be null (disables
  // persistence). Both pointers must outlive this object.
  FilePropertyStore(FileBackend* runtime_backend, FileBackend* persistent_backend);

  [[nodiscard]] int Get(const char* key, char* buf, std::size_t buf_len) override;
  [[nodiscard]] int Set(const char* key, const char* value) override;
  [[nodiscard]] int Delete(const char* key) override;
  [[nodiscard]] int Exists(const char* key) override;
  [[nodiscard]] int ForEach(Visitor fn) override;
  [[nodiscard]] int LoadPersistentProperties() override;

 private:
  [[nodiscard]] int SetRuntimeOnly(const char* key, const char* value);

  FileBackend* runtime_;
  FileBackend* persistent_;  // may be null
};

}  // namespace sysprop::internal
