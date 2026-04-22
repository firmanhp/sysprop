#include "validation.h"

#include <string_view>

#include <sysprop/sysprop.h>

namespace sysprop::internal {

int ValidateKey(std::string_view key) noexcept {
  if (key.empty()) {
    return SYSPROP_ERR_INVALID_KEY;
  }
  if (key.size() >= SYSPROP_MAX_KEY_LENGTH) {
    return SYSPROP_ERR_KEY_TOO_LONG;
  }

  // Must not start or end with a dot.
  if (key.front() == '.' || key.back() == '.') {
    return SYSPROP_ERR_INVALID_KEY;
  }

  bool prev_dot = false;
  for (const unsigned char c : key) {
    const bool is_alnum =
        (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    const bool is_allowed = is_alnum || c == '.' || c == '_' || c == '-';
    if (!is_allowed) {
      return SYSPROP_ERR_INVALID_KEY;
    }

    // No consecutive dots (empty segment between dots).
    if (c == '.' && prev_dot) {
      return SYSPROP_ERR_INVALID_KEY;
    }
    prev_dot = (c == '.');
  }

  return SYSPROP_OK;
}

int ValidateValue(std::string_view value) noexcept {
  if (value.size() >= SYSPROP_MAX_VALUE_LENGTH) {
    return SYSPROP_ERR_VALUE_TOO_LONG;
  }
  // Values are stored as plain-text files; embedded nulls are not supported.
  if (value.find('\0') != std::string_view::npos) {
    return SYSPROP_ERR_INVALID_KEY;
  }
  return SYSPROP_OK;
}

}  // namespace sysprop::internal
