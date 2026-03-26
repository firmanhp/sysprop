#include "property_store.h"

#include <cstring>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <sysprop/sysprop.h>

#include "backend.h"

using sysprop::internal::Backend;
using sysprop::internal::PropertyStore;
using ::testing::_;
using ::testing::Return;
using ::testing::StrEq;

// ── Mock backend ──────────────────────────────────────────────────────────────

class MockBackend : public Backend {
 public:
  MOCK_METHOD(int, Get, (const char* key, char* buf, std::size_t buf_len), (override));
  MOCK_METHOD(int, Set, (const char* key, const char* value), (override));
  MOCK_METHOD(int, Delete, (const char* key), (override));
  MOCK_METHOD(int, Exists, (const char* key), (override));
  MOCK_METHOD(int, ForEach, (Visitor visitor), (override));
};

// ── Helpers ───────────────────────────────────────────────────────────────────

// Returns a gmock action that writes val into the Get() output buffer.
auto GetReturns(const char* val) {
  return [val](const char*, char* buf, std::size_t len) -> int {
    std::strncpy(buf, val, len - 1);
    buf[len - 1] = '\0';
    return static_cast<int>(std::strlen(buf));
  };
}

// ── Basic delegation ──────────────────────────────────────────────────────────

TEST(PropertyStore, GetDelegatesToRuntime) {
  MockBackend runtime;
  PropertyStore store{&runtime, nullptr};

  EXPECT_CALL(runtime, Get(StrEq("test.key"), _, _)).WillOnce(GetReturns("hello"));

  char buf[64];
  EXPECT_GE(store.Get("test.key", buf, sizeof(buf)), 0);
  EXPECT_STREQ("hello", buf);
}

TEST(PropertyStore, SetDelegatesToRuntime) {
  MockBackend runtime;
  PropertyStore store{&runtime, nullptr};

  EXPECT_CALL(runtime, Exists(_)).Times(0);  // not ro.*
  EXPECT_CALL(runtime, Set(StrEq("test.key"), StrEq("val"))).WillOnce(Return(SYSPROP_OK));

  EXPECT_EQ(SYSPROP_OK, store.Set("test.key", "val"));
}

TEST(PropertyStore, DeleteDelegatesToRuntime) {
  MockBackend runtime;
  PropertyStore store{&runtime, nullptr};

  EXPECT_CALL(runtime, Delete(StrEq("test.key"))).WillOnce(Return(SYSPROP_OK));

  EXPECT_EQ(SYSPROP_OK, store.Delete("test.key"));
}

// ── Key validation ────────────────────────────────────────────────────────────

TEST(PropertyStore, GetRejectsInvalidKey) {
  MockBackend runtime;
  PropertyStore store{&runtime, nullptr};
  EXPECT_CALL(runtime, Get(_, _, _)).Times(0);

  char buf[64];
  EXPECT_EQ(SYSPROP_ERR_INVALID_KEY, store.Get("", buf, sizeof(buf)));
  EXPECT_EQ(SYSPROP_ERR_INVALID_KEY, store.Get("bad..key", buf, sizeof(buf)));
}

TEST(PropertyStore, SetRejectsInvalidKey) {
  MockBackend runtime;
  PropertyStore store{&runtime, nullptr};
  EXPECT_CALL(runtime, Set(_, _)).Times(0);

  EXPECT_EQ(SYSPROP_ERR_INVALID_KEY, store.Set("", "v"));
  EXPECT_EQ(SYSPROP_ERR_INVALID_KEY, store.Set(".bad", "v"));
}

// ── ro.* enforcement ──────────────────────────────────────────────────────────

TEST(PropertyStore, RoPropertySetOnce) {
  MockBackend runtime;
  PropertyStore store{&runtime, nullptr};

  EXPECT_CALL(runtime, Exists(StrEq("ro.build.version"))).WillOnce(Return(SYSPROP_ERR_NOT_FOUND));
  EXPECT_CALL(runtime, Set(StrEq("ro.build.version"), StrEq("42"))).WillOnce(Return(SYSPROP_OK));

  EXPECT_EQ(SYSPROP_OK, store.Set("ro.build.version", "42"));
}

TEST(PropertyStore, RoPropertySetTwiceReturnsReadOnly) {
  MockBackend runtime;
  PropertyStore store{&runtime, nullptr};

  EXPECT_CALL(runtime, Exists(StrEq("ro.build.version"))).WillOnce(Return(SYSPROP_OK));
  EXPECT_CALL(runtime, Set(_, _)).Times(0);

  EXPECT_EQ(SYSPROP_ERR_READ_ONLY, store.Set("ro.build.version", "99"));
}

TEST(PropertyStore, DeleteRoPropertyReturnsReadOnly) {
  MockBackend runtime;
  PropertyStore store{&runtime, nullptr};
  EXPECT_CALL(runtime, Delete(_)).Times(0);

  EXPECT_EQ(SYSPROP_ERR_READ_ONLY, store.Delete("ro.my.prop"));
}

// ── persist.* dual-write ──────────────────────────────────────────────────────

TEST(PropertyStore, PersistPropertyWritesToBothBackends) {
  MockBackend runtime;
  MockBackend persistent;
  PropertyStore store{&runtime, &persistent};

  EXPECT_CALL(runtime, Set(StrEq("persist.wifi.ssid"), StrEq("Home"))).WillOnce(Return(SYSPROP_OK));
  EXPECT_CALL(persistent, Set(StrEq("persist.wifi.ssid"), StrEq("Home"))).WillOnce(Return(SYSPROP_OK));

  EXPECT_EQ(SYSPROP_OK, store.Set("persist.wifi.ssid", "Home"));
}

TEST(PropertyStore, PersistBackendFailureIsNonFatal) {
  MockBackend runtime;
  MockBackend persistent;
  PropertyStore store{&runtime, &persistent};

  EXPECT_CALL(runtime, Set(_, _)).WillOnce(Return(SYSPROP_OK));
  EXPECT_CALL(persistent, Set(_, _)).WillOnce(Return(SYSPROP_ERR_IO));

  // Persistent failure must not fail the overall Set.
  EXPECT_EQ(SYSPROP_OK, store.Set("persist.wifi.ssid", "Home"));
}

TEST(PropertyStore, PersistDeleteCascadesToPersistentBackend) {
  MockBackend runtime;
  MockBackend persistent;
  PropertyStore store{&runtime, &persistent};

  EXPECT_CALL(runtime, Delete(StrEq("persist.wifi.ssid"))).WillOnce(Return(SYSPROP_OK));
  EXPECT_CALL(persistent, Delete(StrEq("persist.wifi.ssid"))).WillOnce(Return(SYSPROP_OK));

  EXPECT_EQ(SYSPROP_OK, store.Delete("persist.wifi.ssid"));
}

TEST(PropertyStore, NoPersistentBackendStillWorks) {
  MockBackend runtime;
  PropertyStore store{&runtime, nullptr};

  EXPECT_CALL(runtime, Set(StrEq("persist.wifi.ssid"), StrEq("Home"))).WillOnce(Return(SYSPROP_OK));

  EXPECT_EQ(SYSPROP_OK, store.Set("persist.wifi.ssid", "Home"));
}

// ── LoadPersistentProperties ──────────────────────────────────────────────────

TEST(PropertyStore, LoadPersistentPropertiesIsNoopWithoutPersistentBackend) {
  MockBackend runtime;
  PropertyStore store{&runtime, nullptr};
  EXPECT_CALL(runtime, Set(_, _)).Times(0);
  EXPECT_EQ(0, store.LoadPersistentProperties());
}

TEST(PropertyStore, LoadPersistentPropertiesPopulatesRuntime) {
  MockBackend runtime;
  MockBackend persistent;
  PropertyStore store{&runtime, &persistent};

  EXPECT_CALL(persistent, ForEach(_)).WillOnce([](Backend::Visitor visitor) -> int {
    visitor("persist.a", "1");
    visitor("persist.b", "2");
    return SYSPROP_OK;
  });

  EXPECT_CALL(runtime, Set(StrEq("persist.a"), StrEq("1"))).WillOnce(Return(SYSPROP_OK));
  EXPECT_CALL(runtime, Set(StrEq("persist.b"), StrEq("2"))).WillOnce(Return(SYSPROP_OK));

  EXPECT_EQ(2, store.LoadPersistentProperties());
}
