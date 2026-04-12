#pragma once // NOLINT(llvm-header-guard) -- #pragma once is used throughout; llvm-header-guard requires #ifndef-style guards

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
class FileBackend final { // NOLINT(cppcoreguidelines-special-member-functions,hicpp-special-member-functions) -- default dtor only; FileBackend holds no heap resources so no move/copy needed
 public:
  // base_path must point to an existing directory. The string is copied
  // internally; the caller's buffer need not outlive this constructor call.
  explicit FileBackend(const char* base_path);
  ~FileBackend() = default;

  [[nodiscard]] int Get(const char* key, char* buf, std::size_t buf_len);
  [[nodiscard]] int Set(const char* key, const char* value);
  [[nodiscard]] int Delete(const char* key);
  [[nodiscard]] int Exists(const char* key);

  // Non-owning, non-allocating visitor passed to ForEach.
  // fn must not be null. The callable and its context must outlive the Visitor.
  struct Visitor {
    bool (*fn)(void* ctx, const char* key, const char* value); // NOLINT(misc-non-private-member-variables-in-classes) -- Visitor is a POD-like callback carrier; public members are intentional
    void* ctx; // NOLINT(misc-non-private-member-variables-in-classes)
    bool operator()(const char* key, const char* value) const {
      return fn(ctx, key, value);
    }
  };

  // Iterate over all stored properties. The visitor is called once per entry
  // with null-terminated key and value strings. Iteration stops early if
  // visitor returns false.
  [[nodiscard]] int ForEach(Visitor visitor);

 private:
  // Writes the full path for key into dst. Returns false if the result would
  // exceed dst_len.
  [[nodiscard]] bool BuildPath(char* dst, std::size_t dst_len, const char* key) const noexcept;

  static constexpr std::size_t kMaxBasePathSize = 4096;
  char base_path_[kMaxBasePathSize];
};

// Create a Visitor from any callable (lambda, functor). The callable is held
// by pointer — it must outlive the returned Visitor.
template<typename F>
FileBackend::Visitor MakeVisitor(F& f) {
  return {[](void* ctx, const char* k, const char* v) -> bool {
    return (*static_cast<F*>(ctx))(k, v);
  }, &f};
}

}  // namespace sysprop::internal
