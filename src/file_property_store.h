#pragma once

#include <cstddef>

#include "file_backend.h"
#include <sysprop/property_store.h>

namespace sysprop::internal {

// Policy-enforcing PropertyStore backed by two FileBackend instances.
//
// Policy rules:
//   - ro.*     : read-only — Set() always rejects; SetInit() allows write-once
//               (used exclusively by sysprop-init at boot).
//   - persist.*: all operations go directly to the persistent backend (no
//               runtime copy). Falls back to runtime if persistent is null.
//   - Other    : volatile — only written to the runtime backend.
//
// All key and value inputs are validated before any backend operation.
class FilePropertyStore final : public PropertyStore {
 public:
  // Both backends must outlive this object.
  constexpr FilePropertyStore(FileBackend& runtime_backend,
                              FileBackend& persistent_backend) noexcept
      : runtime_(runtime_backend), persistent_(persistent_backend) {}

  [[nodiscard]] int Get(const char* key, char* buf, std::size_t buf_len) override;
  [[nodiscard]] int Set(const char* key, const char* value) override;
  [[nodiscard]] int SetInit(const char* key, const char* value) override;
  [[nodiscard]] int Delete(const char* key) override;
  [[nodiscard]] int Exists(const char* key) override;
  [[nodiscard]] int ForEach(const std::function<void(const char*, const char*)>& visitor) override;

 private:
  FileBackend& runtime_;
  FileBackend& persistent_;
};

}  // namespace sysprop::internal
