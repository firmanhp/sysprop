#include "file_backend.h"

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>

#include <sysprop/sysprop.h>

namespace sysprop::internal {

namespace {

// Maximum size of a path buffer we construct:
//   base_path (up to PATH_MAX) + '/' + key (SYSPROP_MAX_KEY_LENGTH) + nul
// An extra 64 bytes covers the ".tmp.<key>.<pid>" suffix used during writes.
constexpr std::size_t kPathBufSize = 4096 + SYSPROP_MAX_KEY_LENGTH + 64;
constexpr int kFileMode = 0644;

// RAII wrapper for a POSIX file descriptor.
struct UniqueFd {
  explicit UniqueFd(int fd) noexcept : fd_{fd} {}
  ~UniqueFd() {
    if (fd_ >= 0) {
      ::close(fd_);
    }
  }
  UniqueFd(const UniqueFd&) = delete;
  UniqueFd& operator=(const UniqueFd&) = delete;
  UniqueFd(UniqueFd&&) = delete;
  UniqueFd& operator=(UniqueFd&&) = delete;
  [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }
  [[nodiscard]] int get() const noexcept { return fd_; }

 private:
  int fd_;
};

UniqueFd OpenReadOnly(const char* path) { return UniqueFd{::open(path, O_RDONLY | O_CLOEXEC)}; }

UniqueFd OpenWriteNew(const char* path) {
  return UniqueFd{::open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, kFileMode)};
}

// RAII wrapper for a DIR*.
struct CloseDirFn {
  void operator()(DIR* d) const { ::closedir(d); }
};
using UniqueDir = std::unique_ptr<DIR, CloseDirFn>;

UniqueDir OpenDir(const char* path) { return UniqueDir{::opendir(path)}; }

}  // namespace

FileBackend::FileBackend(
    const char* base_path) {  // NOLINT(cppcoreguidelines-pro-type-member-init,hicpp-member-init) --
                              // base_path_ is a char array; it cannot be member-initialized,
                              // strncpy in the body is the correct idiom
  std::strncpy(base_path_, base_path, kMaxBasePathSize - 1);
  base_path_[kMaxBasePathSize - 1] = '\0';
}

bool FileBackend::BuildPath(char* dst, std::size_t dst_len, const char* key)
    const noexcept {  // NOLINT(readability-non-const-parameter) -- dst is a write-out buffer; const
                      // would be wrong
  const int n = std::snprintf(dst, dst_len, "%s/%s", base_path_, key);
  return n > 0 && static_cast<std::size_t>(n) < dst_len;
}

int FileBackend::Get(const char* key, char* buf, std::size_t buf_len) {
  if (!buf || buf_len == 0) {
    return SYSPROP_ERR_BUFFER_TOO_SMALL;
  }

  char path[kPathBufSize];
  if (!BuildPath(path, sizeof(path), key)) {
    return SYSPROP_ERR_IO;
  }

  const UniqueFd fd = OpenReadOnly(path);
  if (!fd.valid()) {
    return (errno == ENOENT) ? SYSPROP_ERR_NOT_FOUND : SYSPROP_ERR_IO;
  }

  // Read at most buf_len - 1 bytes to leave room for the null terminator.
  const ssize_t n = ::read(fd.get(), buf, buf_len - 1);
  if (n < 0) {
    return SYSPROP_ERR_IO;
  }
  buf[n] = '\0';  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic) -- n is bounds-checked:
                  // read() returns at most buf_len-1
  return static_cast<int>(n);
}

int FileBackend::Set(const char* key, const char* value) {
  char final_path[kPathBufSize];
  char tmp_path[kPathBufSize];
  if (!BuildPath(final_path, sizeof(final_path), key)) {
    return SYSPROP_ERR_IO;
  }

  const int n = std::snprintf(tmp_path, sizeof(tmp_path), "%s/.tmp.%s.%d", base_path_, key,
                              static_cast<int>(::getpid()));
  if (n <= 0 || static_cast<std::size_t>(n) >= sizeof(tmp_path)) {
    return SYSPROP_ERR_IO;
  }

  {
    const UniqueFd fd = OpenWriteNew(tmp_path);
    if (!fd.valid()) {
      return SYSPROP_ERR_IO;
    }

    const std::size_t val_len = std::strlen(value);
    const ssize_t written = ::write(fd.get(), value, val_len);
    if (written < 0 || static_cast<std::size_t>(written) != val_len) {
      ::unlink(tmp_path);
      return SYSPROP_ERR_IO;
    }
  }  // fd closed here before rename

  // Atomic commit: rename is guaranteed atomic on the same filesystem.
  if (::rename(tmp_path, final_path) < 0) {
    ::unlink(tmp_path);
    return SYSPROP_ERR_IO;
  }

  return SYSPROP_OK;
}

int FileBackend::Delete(const char* key) {
  char path[kPathBufSize];
  if (!BuildPath(path, sizeof(path), key)) {
    return SYSPROP_ERR_IO;
  }

  if (::unlink(path) < 0) {
    return (errno == ENOENT) ? SYSPROP_ERR_NOT_FOUND : SYSPROP_ERR_IO;
  }
  return SYSPROP_OK;
}

int FileBackend::Exists(const char* key) {
  char path[kPathBufSize];
  if (!BuildPath(path, sizeof(path), key)) {
    return SYSPROP_ERR_IO;
  }

  return (::access(path, F_OK) == 0) ? SYSPROP_OK : SYSPROP_ERR_NOT_FOUND;
}

int FileBackend::ForEach(Visitor visitor) {
  const UniqueDir dir = OpenDir(base_path_);
  if (!dir) {
    return SYSPROP_ERR_IO;
  }

  // Value read buffer — stack-allocated and reused for every entry.
  char val_buf[SYSPROP_MAX_VALUE_LENGTH];

  const struct dirent* entry = nullptr;
  while ((entry = ::readdir(dir.get())) !=
         nullptr) {  // NOLINT(concurrency-mt-unsafe) -- single-threaded use: ForEach is called from
                     // the main thread before any other threads start
    // Skip hidden files (covers ".", "..", and our ".tmp.*" temp files).
    if (entry->d_name[0] == '.') {
      continue;
    }

    const int rc = Get(entry->d_name, val_buf, sizeof(val_buf));
    if (rc < 0) {
      continue;
    }  // Skip unreadable entries silently.

    if (!visitor(entry->d_name, val_buf)) {
      break;
    }
  }

  return SYSPROP_OK;
}

}  // namespace sysprop::internal
