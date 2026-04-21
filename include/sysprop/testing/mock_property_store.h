#pragma once  // NOLINT(llvm-header-guard) -- #pragma once is used throughout; llvm-header-guard
              // requires #ifndef-style guards

#include <cstring>
#include <string>
#include <unordered_map>

#include <sysprop/sysprop.h>

#include <sysprop/property_store.h>
#include <sysprop/testing/internal.h>

namespace sysprop::internal {

// In-memory property store for testing. No key/value validation; no ro.* or
// persist.* policy enforcement — whatever you set is what you get.
//
// RAII: the constructor installs this store as the active global store, and the
// destructor restores the previous one. This means sysprop_set/sysprop_get/etc.
// go through this mock for the lifetime of the object.
//
// Non-copyable, non-movable: the object owns the global store slot.
//
//   {
//     MockPropertyStore mock;
//     sysprop_set("key", "val");       // hits mock's hash map
//     sysprop_get_int("key", 0);       // reads from mock
//   }  // global store restored here
class MockPropertyStore final : public PropertyStore {
 public:
  MockPropertyStore() : prev_(swap_store(this)) {}
  ~MockPropertyStore() override { swap_store(prev_); }

  MockPropertyStore(const MockPropertyStore&) = delete;
  MockPropertyStore& operator=(const MockPropertyStore&) = delete;
  MockPropertyStore(MockPropertyStore&&) = delete;
  MockPropertyStore& operator=(MockPropertyStore&&) = delete;

  int Get(const char* key, char* buf, std::size_t buf_len) override {
    if (buf_len == 0) { return SYSPROP_ERR_BUFFER_TOO_SMALL; }
    const auto it = map_.find(key);
    if (it == map_.end()) { return SYSPROP_ERR_NOT_FOUND; }
    const std::string& val = it->second;
    const std::size_t copy_len = (val.size() < buf_len) ? val.size() : buf_len - 1;
    std::memcpy(buf, val.data(), copy_len);
    buf[copy_len] = '\0';
    return static_cast<int>(copy_len);
  }

  int Set(const char* key, const char* value) override {
    map_[key] = value;
    return SYSPROP_OK;
  }

  int Delete(const char* key) override {
    const auto it = map_.find(key);
    if (it == map_.end()) { return SYSPROP_ERR_NOT_FOUND; }
    map_.erase(it);
    return SYSPROP_OK;
  }

  int Exists(const char* key) override {
    return map_.count(key) != 0 ? SYSPROP_OK : SYSPROP_ERR_NOT_FOUND;
  }

  int ForEach(Visitor fn) override {
    for (const auto& [k, v] : map_) {
      if (!fn(k.c_str(), v.c_str())) { break; }
    }
    return SYSPROP_OK;
  }

  int LoadPersistentProperties() override { return 0; }

 private:
  std::unordered_map<std::string, std::string> map_;
  PropertyStore* prev_;
};

}  // namespace sysprop::internal
