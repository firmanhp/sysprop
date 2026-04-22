#include "file_property_store.h"

#include <cstdio>
#include <functional>
#include <string_view>

#include <sysprop/sysprop.h>

#include "file_backend.h"
#include "validation.h"

namespace sysprop::internal {

namespace {

constexpr std::string_view kRoPrefix = "ro.";
constexpr std::string_view kPersistPrefix = "persist.";

// string_view::starts_with is C++20; this is the C++17 equivalent.
[[nodiscard]] bool StartsWith(std::string_view str, std::string_view prefix) noexcept {
  return str.substr(0, prefix.size()) == prefix;
}

}  // namespace

int FilePropertyStore::Get(const char* key, char* buf, std::size_t buf_len) {
  if (const int err = ValidateKey(key); err != SYSPROP_OK) {
    return err;
  }
  if (persistent_ != nullptr && StartsWith(key, kPersistPrefix)) {
    return persistent_->Get(key, buf, buf_len);
  }
  return runtime_->Get(key, buf, buf_len);
}

int FilePropertyStore::Set(const char* key, const char* value) {
  if (const int err = ValidateKey(key); err != SYSPROP_OK) {
    return err;
  }
  if (const int err = ValidateValue(value); err != SYSPROP_OK) {
    return err;
  }

  // ro.* properties are always read-only; only SetInit() may write them.
  if (StartsWith(key, kRoPrefix)) {
    return SYSPROP_ERR_READ_ONLY;
  }

  // persist.* properties live entirely in the persistent store.
  if (persistent_ != nullptr && StartsWith(key, kPersistPrefix)) {
    return persistent_->Set(key, value);
  }
  return runtime_->Set(key, value);
}

int FilePropertyStore::SetInit(const char* key, const char* value) {
  if (const int err = ValidateKey(key); err != SYSPROP_OK) {
    return err;
  }
  if (const int err = ValidateValue(value); err != SYSPROP_OK) {
    return err;
  }

  // ro.* properties may be written exactly once (by sysprop-init at boot).
  if (StartsWith(key, kRoPrefix)) {
    if (runtime_->Exists(key) == SYSPROP_OK) {
      return SYSPROP_ERR_READ_ONLY;
    }
    return runtime_->Set(key, value);
  }

  // Non-ro.* keys follow normal Set() routing (persist.* → persistent backend).
  if (persistent_ != nullptr && StartsWith(key, kPersistPrefix)) {
    return persistent_->Set(key, value);
  }
  return runtime_->Set(key, value);
}

int FilePropertyStore::Delete(const char* key) {
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

int FilePropertyStore::Exists(const char* key) {
  if (const int err = ValidateKey(key); err != SYSPROP_OK) {
    return err;
  }
  if (persistent_ != nullptr && StartsWith(key, kPersistPrefix)) {
    return persistent_->Exists(key);
  }
  return runtime_->Exists(key);
}

int FilePropertyStore::ForEach(const std::function<void(const char*, const char*)>& visitor) {
  // Abort on runtime failure rather than delivering a partial listing.
  const int rc = runtime_->ForEach(visitor);
  if (rc != SYSPROP_OK) {
    return rc;
  }
  if (persistent_ != nullptr) {
    return persistent_->ForEach(visitor);
  }
  return SYSPROP_OK;
}

}  // namespace sysprop::internal
