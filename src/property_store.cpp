#include "property_store.h"

#include <cstdio>
#include <string_view>

#include "file_backend.h"  // direct include: FileBackend and MakeVisitor used below

#include <sysprop/sysprop.h>

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
  if (const int err = ValidateKey(key); err != SYSPROP_OK) { return err; }
  return runtime_->Get(key, buf, buf_len);
}

int PropertyStore::Set(const char* key, const char* value) {
  if (const int err = ValidateKey(key); err != SYSPROP_OK) { return err; }
  if (const int err = ValidateValue(value); err != SYSPROP_OK) { return err; }

  // ro.* properties can be set exactly once.
  if (StartsWith(key, kRoPrefix)) {
    if (runtime_->Exists(key) == SYSPROP_OK) { return SYSPROP_ERR_READ_ONLY; }
  }

  if (const int rc = runtime_->Set(key, value); rc != SYSPROP_OK) { return rc; }

  // persist.* properties are additionally written to the persistent store.
  if (persistent_ != nullptr && StartsWith(key, kPersistPrefix)) {
    if (const int prc = persistent_->Set(key, value); prc != SYSPROP_OK) {
      // Non-fatal: log a warning but do not fail the overall Set — the runtime
      // store is the source of truth.
      (void)std::fprintf(stderr, "sysprop: warning: failed to persist property '%s' (err=%d)\n", key,
                         prc);
    }
  }

  return SYSPROP_OK;
}

int PropertyStore::Delete(const char* key) {
  if (const int err = ValidateKey(key); err != SYSPROP_OK) { return err; }

  if (StartsWith(key, kRoPrefix)) { return SYSPROP_ERR_READ_ONLY; }

  const int rc = runtime_->Delete(key);

  if (persistent_ != nullptr && StartsWith(key, kPersistPrefix)) {
    // Best-effort: explicitly discard the result — errors here are non-fatal.
    (void)persistent_->Delete(key);
  }

  return rc;
}

int PropertyStore::Exists(const char* key) {
  if (const int err = ValidateKey(key); err != SYSPROP_OK) { return err; }
  return runtime_->Exists(key);
}

int PropertyStore::ForEachImpl(FileBackend::Visitor visitor) {
  return runtime_->ForEach(visitor);
}

int PropertyStore::SetRuntimeOnly(const char* key, const char* value) {
  return runtime_->Set(key, value);
}

int PropertyStore::LoadPersistentProperties() {
  if (persistent_ == nullptr) { return 0; }

  int load_count = 0;
  // Explicitly discard ForEach's return value: partial iteration failure is
  // non-fatal during boot-time property loading.
  auto fn = [&](const char* key, const char* value) {
    if (SetRuntimeOnly(key, value) == SYSPROP_OK) {
      ++load_count;
    } else {
      (void)std::fprintf(stderr, "sysprop: warning: failed to load persistent property '%s'\n", key);
    }
    return true;  // continue iteration
  };
  (void)persistent_->ForEach(MakeVisitor(fn));

  return load_count;
}

}  // namespace sysprop::internal
