#include "property_store.h"

#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>

#include <gtest/gtest.h>
#include <sys/stat.h>
#include <sysprop/sysprop.h>

#include "file_backend.h"

using sysprop::internal::FileBackend;
using sysprop::internal::PropertyStore;

// ── Fixture ───────────────────────────────────────────────────────────────────

class PropertyStoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    std::string rt = testing::TempDir() + "sysprop_ps_rt_XXXXXX";
    std::string ps = testing::TempDir() + "sysprop_ps_ps_XXXXXX";
    ASSERT_NE(::mkdtemp(rt.data()), nullptr) << strerror(errno);
    ASSERT_NE(::mkdtemp(ps.data()), nullptr) << strerror(errno);
    rt_dir_ = rt;
    ps_dir_ = ps;
    rt_backend_ = FileBackend{rt_dir_.c_str()};
    ps_backend_ = FileBackend{ps_dir_.c_str()};
    store_ = PropertyStore{&rt_backend_, &ps_backend_};
  }

  // Returns true if key exists as a file under dir.
  static bool FileExists(const std::string& dir, const char* key) {
    return ::access((dir + "/" + key).c_str(), F_OK) == 0;
  }

  std::string rt_dir_;
  std::string ps_dir_;
  FileBackend rt_backend_{testing::TempDir().c_str()};  // overwritten in SetUp
  FileBackend ps_backend_{testing::TempDir().c_str()};
  PropertyStore store_{&rt_backend_, &ps_backend_};
  char buf_[SYSPROP_MAX_VALUE_LENGTH] = {};
};

// ── Validation ────────────────────────────────────────────────────────────────

TEST_F(PropertyStoreTest, GetRejectsInvalidKey) {
  EXPECT_EQ(SYSPROP_ERR_INVALID_KEY, store_.Get("", buf_, sizeof(buf_)));
  EXPECT_EQ(SYSPROP_ERR_INVALID_KEY, store_.Get("bad..key", buf_, sizeof(buf_)));
}

TEST_F(PropertyStoreTest, SetRejectsInvalidKey) {
  EXPECT_EQ(SYSPROP_ERR_INVALID_KEY, store_.Set("", "v"));
  EXPECT_EQ(SYSPROP_ERR_INVALID_KEY, store_.Set(".bad", "v"));
}

// ── Basic delegation ──────────────────────────────────────────────────────────

TEST_F(PropertyStoreTest, GetDelegatesToRuntime) {
  ASSERT_EQ(SYSPROP_OK, store_.Set("test.key", "hello"));
  const int n = store_.Get("test.key", buf_, sizeof(buf_));
  ASSERT_GE(n, 0);
  EXPECT_STREQ("hello", buf_);
}

TEST_F(PropertyStoreTest, SetDelegatesToRuntime) {
  ASSERT_EQ(SYSPROP_OK, store_.Set("test.key", "val"));
  EXPECT_TRUE(FileExists(rt_dir_, "test.key"));
}

TEST_F(PropertyStoreTest, DeleteDelegatesToRuntime) {
  ASSERT_EQ(SYSPROP_OK, store_.Set("test.key", "val"));
  ASSERT_EQ(SYSPROP_OK, store_.Delete("test.key"));
  EXPECT_EQ(SYSPROP_ERR_NOT_FOUND, store_.Get("test.key", buf_, sizeof(buf_)));
}

TEST_F(PropertyStoreTest, ExistsDelegatesToRuntime) {
  EXPECT_EQ(SYSPROP_ERR_NOT_FOUND, store_.Exists("test.key"));
  ASSERT_EQ(SYSPROP_OK, store_.Set("test.key", "v"));
  EXPECT_EQ(SYSPROP_OK, store_.Exists("test.key"));
}

TEST_F(PropertyStoreTest, ExistsRejectsInvalidKey) {
  EXPECT_EQ(SYSPROP_ERR_INVALID_KEY, store_.Exists(""));
}

TEST_F(PropertyStoreTest, ForEachIteratesRuntimeProperties) {
  ASSERT_EQ(SYSPROP_OK, store_.Set("a.x", "1"));
  ASSERT_EQ(SYSPROP_OK, store_.Set("b.y", "2"));
  ASSERT_EQ(SYSPROP_OK, store_.Set("c.z", "3"));

  int count = 0;
  (void)store_.ForEach([&](const char*, const char*) {
    ++count;
    return true;
  });
  EXPECT_EQ(3, count);
}

TEST_F(PropertyStoreTest, ForEachEarlyStop) {
  ASSERT_EQ(SYSPROP_OK, store_.Set("a.x", "1"));
  ASSERT_EQ(SYSPROP_OK, store_.Set("b.y", "2"));
  ASSERT_EQ(SYSPROP_OK, store_.Set("c.z", "3"));

  int count = 0;
  (void)store_.ForEach([&](const char*, const char*) {
    ++count;
    return false;
  });
  EXPECT_EQ(1, count);
}

TEST_F(PropertyStoreTest, SetRejectsValueTooLong) {
  const std::string too_long(SYSPROP_MAX_VALUE_LENGTH, 'x');
  EXPECT_EQ(SYSPROP_ERR_VALUE_TOO_LONG, store_.Set("test.key", too_long.c_str()));
  // Key must not have been created.
  EXPECT_EQ(SYSPROP_ERR_NOT_FOUND, store_.Get("test.key", buf_, sizeof(buf_)));
}

TEST_F(PropertyStoreTest, DeleteNonExistentKeyReturnsNotFound) {
  EXPECT_EQ(SYSPROP_ERR_NOT_FOUND, store_.Delete("no.such.key"));
}

TEST_F(PropertyStoreTest, DeleteInvalidKeyReturnsError) {
  EXPECT_EQ(SYSPROP_ERR_INVALID_KEY, store_.Delete(""));
  EXPECT_EQ(SYSPROP_ERR_INVALID_KEY, store_.Delete("bad..key"));
}

// ── ro.* enforcement ──────────────────────────────────────────────────────────

// ── ro.* attack hardening ──────────────────────────────────────────────────────

TEST_F(PropertyStoreTest, RoOverwriteWithSameValueIsStillRejected) {
  // No "idempotent overwrite" loophole: same value must still be blocked.
  ASSERT_EQ(SYSPROP_OK, store_.Set("ro.hw.id", "abc123"));
  EXPECT_EQ(SYSPROP_ERR_READ_ONLY, store_.Set("ro.hw.id", "abc123"));
}

TEST_F(PropertyStoreTest, RoRepeatedOverwriteAttemptsAllFail) {
  ASSERT_EQ(SYSPROP_OK, store_.Set("ro.hw.board", "original"));
  for (int i = 0; i < 20; ++i) {
    EXPECT_EQ(SYSPROP_ERR_READ_ONLY, store_.Set("ro.hw.board", "hacked"));
  }
  const int n = store_.Get("ro.hw.board", buf_, sizeof(buf_));
  ASSERT_GE(n, 0);
  EXPECT_STREQ("original", buf_);
}

TEST_F(PropertyStoreTest, RoDeleteRepeatedlyStillFails) {
  ASSERT_EQ(SYSPROP_OK, store_.Set("ro.sealed", "yes"));
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(SYSPROP_ERR_READ_ONLY, store_.Delete("ro.sealed"));
  }
  EXPECT_EQ(SYSPROP_OK, store_.Exists("ro.sealed"));
}

TEST_F(PropertyStoreTest, RoEmptyValueIsImmutable) {
  // An ro. property set to "" is still locked — emptiness is not a bypass.
  ASSERT_EQ(SYSPROP_OK, store_.Set("ro.empty.prop", ""));
  EXPECT_EQ(SYSPROP_ERR_READ_ONLY, store_.Set("ro.empty.prop", "now-non-empty"));
  const int n = store_.Get("ro.empty.prop", buf_, sizeof(buf_));
  ASSERT_GE(n, 0);
  EXPECT_STREQ("", buf_);
}

// ── Prefix-boundary corner cases ─────────────────────────────────────────────

TEST_F(PropertyStoreTest, BareRoKeyWithoutDotIsNotReadOnly) {
  // "ro" without the trailing dot is NOT subject to the ro.* policy.
  ASSERT_EQ(SYSPROP_OK, store_.Set("ro", "first"));
  ASSERT_EQ(SYSPROP_OK, store_.Set("ro", "second"));
  const int n = store_.Get("ro", buf_, sizeof(buf_));
  ASSERT_GE(n, 0);
  EXPECT_STREQ("second", buf_);
}

TEST_F(PropertyStoreTest, BarePeristKeyWithoutDotIsNotPersisted) {
  // "persist" without the trailing dot is NOT dual-written to the persistent store.
  ASSERT_EQ(SYSPROP_OK, store_.Set("persist", "val"));
  EXPECT_TRUE(FileExists(rt_dir_, "persist"));
  EXPECT_FALSE(FileExists(ps_dir_, "persist"));
}

TEST_F(PropertyStoreTest, RoPersistKeyIsReadOnlyAndNotPersisted) {
  // ro.persist.* starts with "ro." so read-only wins; "persist." is not matched.
  ASSERT_EQ(SYSPROP_OK, store_.Set("ro.persist.cfg", "locked"));
  EXPECT_EQ(SYSPROP_ERR_READ_ONLY, store_.Set("ro.persist.cfg", "hacked"));
  EXPECT_FALSE(FileExists(ps_dir_, "ro.persist.cfg"));
}

// ── Mutation idempotency ──────────────────────────────────────────────────────

TEST_F(PropertyStoreTest, MutableKeyManyOverwritesLastValueWins) {
  for (int i = 0; i < 50; ++i) {
    ASSERT_EQ(SYSPROP_OK, store_.Set("volatile.counter", std::to_string(i).c_str()));
  }
  const int n = store_.Get("volatile.counter", buf_, sizeof(buf_));
  ASSERT_GE(n, 0);
  EXPECT_STREQ("49", buf_);
}

TEST_F(PropertyStoreTest, OverwriteMaxValueWithEmptyLeavesEmpty) {
  // Replacing a max-length value with "" must leave exactly ""; no stale bytes.
  const std::string big(SYSPROP_MAX_VALUE_LENGTH - 1, 'z');
  ASSERT_EQ(SYSPROP_OK, store_.Set("mutable.key", big.c_str()));
  ASSERT_EQ(SYSPROP_OK, store_.Set("mutable.key", ""));
  EXPECT_EQ(0, store_.Get("mutable.key", buf_, sizeof(buf_)));
  EXPECT_STREQ("", buf_);
}

// ── persist.* attack scenarios ────────────────────────────────────────────────

TEST_F(PropertyStoreTest, PersistDeleteAndRecreateUpdatesBothBackends) {
  ASSERT_EQ(SYSPROP_OK, store_.Set("persist.cfg", "old"));
  ASSERT_EQ(SYSPROP_OK, store_.Delete("persist.cfg"));
  ASSERT_EQ(SYSPROP_OK, store_.Set("persist.cfg", "new"));
  const int n = store_.Get("persist.cfg", buf_, sizeof(buf_));
  ASSERT_GE(n, 0);
  EXPECT_STREQ("new", buf_);
  EXPECT_TRUE(FileExists(rt_dir_, "persist.cfg"));
  EXPECT_TRUE(FileExists(ps_dir_, "persist.cfg"));
}

TEST_F(PropertyStoreTest, RoPropertyLoadedFromPersistentBecomesImmutable) {
  // A ro.* property pre-written to the persistent store (e.g., at factory) must
  // become immutable in the runtime store after LoadPersistentProperties().
  ASSERT_EQ(SYSPROP_OK, ps_backend_.Set("ro.hw.sku", "prod-001"));
  EXPECT_GT(store_.LoadPersistentProperties(), 0);
  EXPECT_EQ(SYSPROP_ERR_READ_ONLY, store_.Set("ro.hw.sku", "evil"));
  const int n = store_.Get("ro.hw.sku", buf_, sizeof(buf_));
  ASSERT_GE(n, 0);
  EXPECT_STREQ("prod-001", buf_);
}

// ─────────────────────────────────────────────────────────────────────────────

TEST_F(PropertyStoreTest, RoPropertySetOnce) {
  EXPECT_EQ(SYSPROP_OK, store_.Set("ro.build.version", "42"));
  const int n = store_.Get("ro.build.version", buf_, sizeof(buf_));
  ASSERT_GE(n, 0);
  EXPECT_STREQ("42", buf_);
}

TEST_F(PropertyStoreTest, RoPropertySetTwiceReturnsReadOnly) {
  ASSERT_EQ(SYSPROP_OK, store_.Set("ro.build.version", "42"));
  EXPECT_EQ(SYSPROP_ERR_READ_ONLY, store_.Set("ro.build.version", "99"));

  // Value must be unchanged.
  const int n = store_.Get("ro.build.version", buf_, sizeof(buf_));
  ASSERT_GE(n, 0);
  EXPECT_STREQ("42", buf_);
}

TEST_F(PropertyStoreTest, DeleteRoPropertyReturnsReadOnly) {
  ASSERT_EQ(SYSPROP_OK, store_.Set("ro.my.prop", "value"));
  EXPECT_EQ(SYSPROP_ERR_READ_ONLY, store_.Delete("ro.my.prop"));
  // Key must still exist.
  EXPECT_TRUE(FileExists(rt_dir_, "ro.my.prop"));
}

// ── persist.* dual-write ──────────────────────────────────────────────────────

TEST_F(PropertyStoreTest, PersistPropertyWritesToBothBackends) {
  ASSERT_EQ(SYSPROP_OK, store_.Set("persist.wifi.ssid", "Home"));
  EXPECT_TRUE(FileExists(rt_dir_, "persist.wifi.ssid"));
  EXPECT_TRUE(FileExists(ps_dir_, "persist.wifi.ssid"));
}

TEST_F(PropertyStoreTest, PersistBackendFailureIsNonFatal) {
  // Make the persistent dir unwritable so FileBackend::Set fails there.
  ASSERT_EQ(0, ::chmod(ps_dir_.c_str(), 0555)) << strerror(errno);
  EXPECT_EQ(SYSPROP_OK, store_.Set("persist.wifi.ssid", "Home"));
  // Runtime must still have the value.
  EXPECT_TRUE(FileExists(rt_dir_, "persist.wifi.ssid"));
  (void)::chmod(ps_dir_.c_str(), 0755);  // restore for TearDown
}

TEST_F(PropertyStoreTest, PersistDeleteCascadesToPersistentBackend) {
  ASSERT_EQ(SYSPROP_OK, store_.Set("persist.wifi.ssid", "Home"));
  ASSERT_EQ(SYSPROP_OK, store_.Delete("persist.wifi.ssid"));
  EXPECT_FALSE(FileExists(rt_dir_, "persist.wifi.ssid"));
  EXPECT_FALSE(FileExists(ps_dir_, "persist.wifi.ssid"));
}

// Delete of persist.* when persistent backend delete fails: runtime delete
// still succeeds, so SYSPROP_OK is returned (persistent error is discarded).
TEST_F(PropertyStoreTest, PersistDeletePersistentFailureReturnsOk) {
  if (::getuid() == 0) {
    GTEST_SKIP() << "root bypasses permission checks";
  }
  ASSERT_EQ(SYSPROP_OK, store_.Set("persist.wifi.ssid", "Home"));
  // Make the persistent dir unwritable so Delete fails there.
  ASSERT_EQ(0, ::chmod(ps_dir_.c_str(), 0555)) << strerror(errno);
  // Runtime delete still succeeds → overall return is SYSPROP_OK.
  EXPECT_EQ(SYSPROP_OK, store_.Delete("persist.wifi.ssid"));
  EXPECT_FALSE(FileExists(rt_dir_, "persist.wifi.ssid"));
  // Persistent file could not be removed; that's acceptable.
  ::chmod(ps_dir_.c_str(), 0755);
}

TEST_F(PropertyStoreTest, NoPersistentBackendStillWorks) {
  PropertyStore no_ps{&rt_backend_, nullptr};
  EXPECT_EQ(SYSPROP_OK, no_ps.Set("persist.wifi.ssid", "Home"));
  EXPECT_TRUE(FileExists(rt_dir_, "persist.wifi.ssid"));
  EXPECT_FALSE(FileExists(ps_dir_, "persist.wifi.ssid"));
}

// ── LoadPersistentProperties ──────────────────────────────────────────────────

TEST_F(PropertyStoreTest, LoadPersistentPropertiesIsNoopWithoutPersistentBackend) {
  PropertyStore no_ps{&rt_backend_, nullptr};
  EXPECT_EQ(0, no_ps.LoadPersistentProperties());
}

TEST_F(PropertyStoreTest, LoadPersistentPropertiesPopulatesRuntime) {
  // Pre-populate the persistent dir directly via ps_backend_.
  ASSERT_EQ(SYSPROP_OK, ps_backend_.Set("persist.a", "1"));
  ASSERT_EQ(SYSPROP_OK, ps_backend_.Set("persist.b", "2"));

  // Fresh runtime dir — nothing loaded yet.
  EXPECT_EQ(SYSPROP_ERR_NOT_FOUND, store_.Get("persist.a", buf_, sizeof(buf_)));

  EXPECT_EQ(2, store_.LoadPersistentProperties());

  const int n = store_.Get("persist.a", buf_, sizeof(buf_));
  ASSERT_GE(n, 0);
  EXPECT_STREQ("1", buf_);
}

// ── ForEach error propagation ─────────────────────────────────────────────────

// When the runtime backend's directory cannot be read, ForEach returns an error
// and the error propagates through PropertyStore::ForEach.
TEST_F(PropertyStoreTest, ForEachPropagatesBackendError) {
  if (::getuid() == 0) {
    GTEST_SKIP() << "root bypasses permission checks";
  }
  ASSERT_EQ(SYSPROP_OK, store_.Set("hw.ok", "yes"));
  // Remove execute (search) permission on the runtime dir so opendir() fails.
  ASSERT_EQ(0, ::chmod(rt_dir_.c_str(), 0000)) << strerror(errno);
  int count = 0;
  const int rc = store_.ForEach([&](const char*, const char*) {
    ++count;
    return true;
  });
  ::chmod(rt_dir_.c_str(), 0755);
  EXPECT_LT(rc, 0);  // should be SYSPROP_ERR_IO
  EXPECT_EQ(0, count);
}
