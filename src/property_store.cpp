#include "property_store.h"

#include <cstdio>
#include <string_view>

#include <sysprop/sysprop.h>

#include "file_backend.h"  // direct include: FileBackend and MakeVisitor used below
#include "validation.h"

namespace sysprop::internal {

namespace {

// Use string_view constants: compile-time size(), no pointer decay, no strlen.
constexpr std::string_view kRoPrefix = "ro.";
constexpr std::string_view kPersistPrefix = "persist.";

// C++17: string_view::substr is O(1) — no copy. Compare prefix against the
// leading bytes of str without invoking strlen or strncmp.
[[nodiscard]] bool StartsWith(std::string_view str, std::string_view prefix) noexcept {
  return str.substr(0, prefix.size()) == prefix;
}

}  // namespace

PropertyStore::PropertyStore(FileBackend* runtime_backend, FileBackend* persistent_backend)
    : runtime_(runtime_backend), persistent_(persistent_backend) {}

int PropertyStore::Get(const char* key, char* buf, std::size_t buf_len) {
  if (const int err = ValidateKey(key); err != SYSPROP_OK) {
    return err;
  }
  if (persistent_ != nullptr && StartsWith(key, kPersistPrefix)) {
    return persistent_->Get(key, buf, buf_len);
  }
  return runtime_->Get(key, buf, buf_len);
}

int PropertyStore::Set(const char* key, const char* value) {
  if (const int err = ValidateKey(key); err != SYSPROP_OK) {
    return err;
  }
  if (const int err = ValidateValue(value); err != SYSPROP_OK) {
    return err;
  }

  // ro.* properties can be set exactly once.
  if (StartsWith(key, kRoPrefix)) {
    if (runtime_->Exists(key) == SYSPROP_OK) {
      return SYSPROP_ERR_READ_ONLY;
    }
  }

  // persist.* properties live entirely in the persistent store.
  if (persistent_ != nullptr && StartsWith(key, kPersistPrefix)) {
    return persistent_->Set(key, value);
  }
  return runtime_->Set(key, value);
}

int PropertyStore::Delete(const char* key) {
  if (const int err = ValidateKey(key); err != SYSPROP_OK) {
    return err;
  }

  if (StartsWith(key, kRoPrefix)) {
    return SYSPROP_ERR_READ_ONLY;
  }

  if (persistent_ != nullptr && StartsWith(key, kPersistPrefix)) {
    return persistent_->Delete(key);
  }
  return runtime_->Delete(key);
}

int PropertyStore::Exists(const char* key) {
  if (const int err = ValidateKey(key); err != SYSPROP_OK) {
    return err;
  }
  if (persistent_ != nullptr && StartsWith(key, kPersistPrefix)) {
    return persistent_->Exists(key);
  }
  return runtime_->Exists(key);
}

int PropertyStore::ForEachImpl(FileBackend::Visitor visitor) {
  if (persistent_ == nullptr) {
    return runtime_->ForEach(visitor);
  }

  // Iterate runtime first, tracking early stop, then persistent.
  bool stopped = false;
  auto track = [&](const char* k, const char* v) -> bool {
    const bool cont = visitor(k, v);
    if (!cont) stopped = true;
    return cont;
  };
  auto tracking_visitor = MakeVisitor(track);

  const int rc = runtime_->ForEach(tracking_visitor);
  if (rc < 0 || stopped) return rc;

  return persistent_->ForEach(visitor);
}

int PropertyStore::SetRuntimeOnly(const char* key, const char* value) {
  return runtime_->Set(key, value);
}

int PropertyStore::LoadPersistentProperties() {
  if (persistent_ == nullptr) {
    return 0;
  }

  int load_count = 0;
  // Explicitly discard ForEach's return value: partial iteration failure is
  // non-fatal during boot-time property loading.
  auto fn = [&](const char* key, const char* value) {
    // persist.* keys are accessed directly from persistent_; skip loading into runtime_.
    if (StartsWith(key, kPersistPrefix)) {
      return true;
    }
    if (SetRuntimeOnly(key, value) == SYSPROP_OK) {
      ++load_count;
    } else {
      (void)std::fprintf(stderr, "sysprop: warning: failed to load persistent property '%s'\n",
                         key);
    }
    return true;
  };
  (void)persistent_->ForEach(MakeVisitor(fn));

  return load_count;
}

}  // namespace sysprop::internal
