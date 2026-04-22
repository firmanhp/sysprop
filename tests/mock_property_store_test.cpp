#include <sysprop/testing/mock_property_store.h>

#include <gtest/gtest.h>
#include <sysprop/sysprop.h>

using sysprop::testing::MockPropertyStore;

// ── Direct usage ──────────────────────────────────────────────────────────────

TEST(MockPropertyStoreTest, SetAndGet) {
  MockPropertyStore mock;
  ASSERT_EQ(SYSPROP_OK, mock.Set("key.a", "hello"));
  char buf[SYSPROP_MAX_VALUE_LENGTH];
  const int n = mock.Get("key.a", buf, sizeof(buf));
  ASSERT_GE(n, 0);
  EXPECT_STREQ("hello", buf);
}

TEST(MockPropertyStoreTest, GetMissingReturnsNotFound) {
  MockPropertyStore mock;
  char buf[SYSPROP_MAX_VALUE_LENGTH];
  EXPECT_EQ(SYSPROP_ERR_NOT_FOUND, mock.Get("no.such.key", buf, sizeof(buf)));
}

TEST(MockPropertyStoreTest, GetBufLenZeroReturnsBufferTooSmall) {
  MockPropertyStore mock;
  ASSERT_EQ(SYSPROP_OK, mock.Set("key.x", "val"));
  char dummy = '\0';
  EXPECT_EQ(SYSPROP_ERR_BUFFER_TOO_SMALL, mock.Get("key.x", &dummy, 0));
}

TEST(MockPropertyStoreTest, DeleteRemovesKey) {
  MockPropertyStore mock;
  ASSERT_EQ(SYSPROP_OK, mock.Set("key.del", "v"));
  ASSERT_EQ(SYSPROP_OK, mock.Delete("key.del"));
  char buf[SYSPROP_MAX_VALUE_LENGTH];
  EXPECT_EQ(SYSPROP_ERR_NOT_FOUND, mock.Get("key.del", buf, sizeof(buf)));
}

TEST(MockPropertyStoreTest, DeleteMissingReturnsNotFound) {
  MockPropertyStore mock;
  EXPECT_EQ(SYSPROP_ERR_NOT_FOUND, mock.Delete("no.such.key"));
}

TEST(MockPropertyStoreTest, ExistsReturnsOkAfterSet) {
  MockPropertyStore mock;
  EXPECT_EQ(SYSPROP_ERR_NOT_FOUND, mock.Exists("key.e"));
  ASSERT_EQ(SYSPROP_OK, mock.Set("key.e", "1"));
  EXPECT_EQ(SYSPROP_OK, mock.Exists("key.e"));
}

TEST(MockPropertyStoreTest, OverwriteLastValueWins) {
  MockPropertyStore mock;
  ASSERT_EQ(SYSPROP_OK, mock.Set("key.ow", "first"));
  ASSERT_EQ(SYSPROP_OK, mock.Set("key.ow", "second"));
  char buf[SYSPROP_MAX_VALUE_LENGTH];
  const int n = mock.Get("key.ow", buf, sizeof(buf));
  ASSERT_GE(n, 0);
  EXPECT_STREQ("second", buf);
}

TEST(MockPropertyStoreTest, NoPolicyEnforcement) {
  // MockPropertyStore does not enforce ro.* or persist.* policies.
  MockPropertyStore mock;
  ASSERT_EQ(SYSPROP_OK, mock.Set("ro.hw.id", "v1"));
  ASSERT_EQ(SYSPROP_OK, mock.Set("ro.hw.id", "v2"));  // no ERR_READ_ONLY
  char buf[SYSPROP_MAX_VALUE_LENGTH];
  const int n = mock.Get("ro.hw.id", buf, sizeof(buf));
  ASSERT_GE(n, 0);
  EXPECT_STREQ("v2", buf);
}

TEST(MockPropertyStoreTest, ForEachVisitsAllKeys) {
  MockPropertyStore mock;
  ASSERT_EQ(SYSPROP_OK, mock.Set("a.x", "1"));
  ASSERT_EQ(SYSPROP_OK, mock.Set("b.y", "2"));
  ASSERT_EQ(SYSPROP_OK, mock.Set("c.z", "3"));

  int count = 0;
  (void)mock.ForEach([&](const char*, const char*) {
    ++count;
    return true;
  });
  EXPECT_EQ(3, count);
}

TEST(MockPropertyStoreTest, ForEachEarlyStop) {
  MockPropertyStore mock;
  ASSERT_EQ(SYSPROP_OK, mock.Set("a.x", "1"));
  ASSERT_EQ(SYSPROP_OK, mock.Set("b.y", "2"));
  ASSERT_EQ(SYSPROP_OK, mock.Set("c.z", "3"));

  int count = 0;
  (void)mock.ForEach([&](const char*, const char*) {
    ++count;
    return false;
  });
  EXPECT_EQ(1, count);
}

// ── RAII injection — sysprop_xxx() hits the mock ─────────────────────────────

TEST(MockPropertyStoreTest, SyspropSetAndGetHitMock) {
  MockPropertyStore mock;
  ASSERT_EQ(SYSPROP_OK, sysprop_set("mock.raii.key", "injected"));
  char buf[SYSPROP_MAX_VALUE_LENGTH];
  ASSERT_GE(sysprop_get("mock.raii.key", buf, sizeof(buf)), 0);
  EXPECT_STREQ("injected", buf);
}

TEST(MockPropertyStoreTest, SyspropGetIntHitsMock) {
  MockPropertyStore mock;
  ASSERT_EQ(SYSPROP_OK, sysprop_set("mock.int.key", "42"));
  EXPECT_EQ(42, sysprop_get_int("mock.int.key", 0));
}

TEST(MockPropertyStoreTest, SyspropGetBoolHitsMock) {
  MockPropertyStore mock;
  ASSERT_EQ(SYSPROP_OK, sysprop_set("mock.bool.key", "true"));
  EXPECT_EQ(1, sysprop_get_bool("mock.bool.key", 0));
}

TEST(MockPropertyStoreTest, SyspropGetFloatHitsMock) {
  MockPropertyStore mock;
  ASSERT_EQ(SYSPROP_OK, sysprop_set("mock.float.key", "3.14"));
  EXPECT_FLOAT_EQ(3.14f, sysprop_get_float("mock.float.key", 0.0f));
}

TEST(MockPropertyStoreTest, SyspropDeleteHitsMock) {
  MockPropertyStore mock;
  ASSERT_EQ(SYSPROP_OK, sysprop_set("mock.del.key", "v"));
  ASSERT_EQ(SYSPROP_OK, sysprop_delete("mock.del.key"));
  char buf[SYSPROP_MAX_VALUE_LENGTH];
  EXPECT_LT(sysprop_get("mock.del.key", buf, sizeof(buf)), 0);
}

TEST(MockPropertyStoreTest, MockRestoredAfterScopeExit) {
  // After the mock is destroyed, sysprop calls should no longer hit it.
  // Since no sysprop_init() is called in this test process (UninitializedApi
  // tests run in separate processes per ctest), the result depends on
  // initialization state. We verify the key set through the mock is gone.
  {
    MockPropertyStore mock;
    ASSERT_EQ(SYSPROP_OK, sysprop_set("mock.scope.key", "live"));
  }
  // After mock destruction, sysprop_get either returns ERR_NOT_INITIALIZED
  // (no singleton) or ERR_NOT_FOUND (singleton present but key not in it).
  char buf[SYSPROP_MAX_VALUE_LENGTH];
  EXPECT_LT(sysprop_get("mock.scope.key", buf, sizeof(buf)), 0);
}
