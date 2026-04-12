#pragma once  // NOLINT(llvm-header-guard) -- #pragma once is used throughout; llvm-header-guard
              // requires #ifndef-style guards

#include <string_view>

namespace sysprop::internal {

// Returns OK (0) if key is valid, or a negative Error code otherwise.
// Valid keys:
//   - Length in [1, kMaxKeyLength]
//   - Characters restricted to [a-zA-Z0-9._-]
//   - Dot-separated segments, each segment non-empty (no leading/trailing dot,
//     no consecutive dots)
[[nodiscard]] int ValidateKey(std::string_view key) noexcept;

// Returns OK (0) if value is valid, or a negative Error code otherwise.
// Valid values:
//   - Length in [0, kMaxValueLength]  (empty value is allowed)
//   - No embedded null bytes
[[nodiscard]] int ValidateValue(std::string_view value) noexcept;

}  // namespace sysprop::internal
