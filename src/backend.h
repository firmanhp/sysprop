#pragma once

#include <cstddef>
#include <functional>

namespace sysprop::internal {

// Abstract storage backend. Internal to the library; not part of the public API.
//
// All methods return:
//   - OK (0) on success
//   - A negative Error code on failure
//   - Get() returns the number of bytes written into buf on success (>= 0)
class Backend {
 public:
  virtual ~Backend() = default;

  // Read value for key into caller-provided buffer.
  // Returns number of bytes written (not including null terminator), or Error.
  // The output is always null-terminated if buf_len > 0.
  [[nodiscard]] virtual int Get(const char* key, char* buf, std::size_t buf_len) = 0;

  // Write value for key. value must be a null-terminated string.
  [[nodiscard]] virtual int Set(const char* key, const char* value) = 0;

  // Delete a key. Returns ERR_NOT_FOUND if absent.
  [[nodiscard]] virtual int Delete(const char* key) = 0;

  // Returns OK if key exists, ERR_NOT_FOUND otherwise.
  [[nodiscard]] virtual int Exists(const char* key) = 0;

  // Iterate over all stored properties. The visitor is called once per entry
  // with null-terminated key and value strings. Iteration stops early if
  // visitor returns false.
  using Visitor = std::function<bool(const char* key, const char* value)>;
  [[nodiscard]] virtual int ForEach(Visitor visitor) = 0;
};

}  // namespace sysprop::internal
