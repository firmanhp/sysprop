#pragma once

#include <string>

#include "backend.h"

namespace sysprop::internal {

// File-based property backend. Each property is stored as a file named after
// the key (e.g., base_path/ro.build.version).
//
// Atomicity:
//   Writes are atomic: the value is written to a temporary file
//   (.tmp.<key>.<pid>) and then rename(2)'d into place. rename(2) is
//   guaranteed to be atomic on the same filesystem by POSIX, so concurrent
//   readers will see either the old value or the new value — never a partial
//   write.
//
//   Reads obtain a consistent snapshot: open(2) + read(2) + close(2). A file
//   descriptor refers to an inode, not a directory entry, so a reader that
//   opens the file before a rename completes still reads the old value
//   consistently; a reader that opens after sees the new value.
//
// Thread safety:
//   All public methods are safe to call concurrently. Multiple simultaneous
//   reads are always safe. Concurrent writes to different keys are safe.
//   Concurrent writes to the same key are safe (last rename wins).
class FileBackend final : public Backend {
 public:
  // base_path must be an existing directory.
  explicit FileBackend(std::string base_path);
  ~FileBackend() override = default;

  [[nodiscard]] int Get(const char* key, char* buf, std::size_t buf_len) override;
  [[nodiscard]] int Set(const char* key, const char* value) override;
  [[nodiscard]] int Delete(const char* key) override;
  [[nodiscard]] int Exists(const char* key) override;
  [[nodiscard]] int ForEach(Visitor visitor) override;

 private:
  // Writes the full path for key into dst. Returns false if the result would
  // exceed dst_len.
  [[nodiscard]] bool BuildPath(char* dst, std::size_t dst_len, const char* key) const noexcept;

  std::string base_path_;
};

}  // namespace sysprop::internal
