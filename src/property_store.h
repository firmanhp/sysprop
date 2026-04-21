#pragma once  // NOLINT(llvm-header-guard) -- #pragma once is used throughout; llvm-header-guard
              // requires #ifndef-style guards

#include <cstddef>

#include "file_backend.h"

namespace sysprop::internal {

// PropertyStore enforces property policy on top of raw storage backends.
//
// Policy rules:
//   - ro.*     : can be set exactly once; subsequent sets return ERR_READ_ONLY.
//   - persist.*: all operations go directly to the persistent backend (no
//               runtime copy). Falls back to runtime if persistent is null.
//   - Other    : volatile — only written to the runtime backend.
//
// All key and value inputs are validated before any backend operation.
class PropertyStore {
 public:
  // runtime_backend is required. persistent_backend may be null (disables
  // persistence). Both pointers must outlive this object.
  PropertyStore(FileBackend* runtime_backend, FileBackend* persistent_backend);

  [[nodiscard]] int Get(const char* key, char* buf, std::size_t buf_len);
  [[nodiscard]] int Set(const char* key, const char* value);
  [[nodiscard]] int Delete(const char* key);
  [[nodiscard]] int Exists(const char* key);

  // Iterate over all properties (runtime and persistent).
  // Accepts any callable: ForEach([&](const char* key, const char* value) { ... });
  template <typename F>
  [[nodiscard]] int ForEach(F f) {
    auto v = MakeVisitor(f);
    return ForEachImpl(v);
  }

  // Load non-persist properties from persistent_backend into runtime_backend
  // (e.g., factory-set ro.* keys). persist.* keys are skipped — they are
  // accessed directly from persistent_backend at runtime.
  // No-op if persistent_backend is null. Returns the number of loaded
  // properties on success, or a negative error code.
  [[nodiscard]] int LoadPersistentProperties();

 private:
  [[nodiscard]] int ForEachImpl(FileBackend::Visitor visitor);
  [[nodiscard]] int SetRuntimeOnly(const char* key, const char* value);

  FileBackend* runtime_;
  FileBackend* persistent_;  // may be null
};

}  // namespace sysprop::internal
