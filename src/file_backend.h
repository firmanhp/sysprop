#pragma once

#include <cstddef>

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
class FileBackend final {
 public:
  // base_path must point to an existing directory. The string is copied
  // internally; the caller's buffer need not outlive this constructor call.
  explicit FileBackend(const char* base_path);
  ~FileBackend() = default;
  FileBackend(const FileBackend&) = delete;
  FileBackend& operator=(const FileBackend&) = delete;
  FileBackend(FileBackend&&) = default;
  FileBackend& operator=(FileBackend&&) = default;

  [[nodiscard]] int Get(const char* key, char* buf, std::size_t buf_len);
  [[nodiscard]] int Set(const char* key, const char* value);
  [[nodiscard]] int Delete(const char* key);
  [[nodiscard]] int Exists(const char* key);

 private:
  // Writes the full path for key into dst. Returns false if the result would
  // exceed dst_len.
  [[nodiscard]] bool BuildPath(char* dst, std::size_t dst_len, const char* key) const noexcept;

  static constexpr std::size_t kMaxBasePathSize = 4096;
  char base_path_[kMaxBasePathSize];
};

}  // namespace sysprop::internal
