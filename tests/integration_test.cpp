#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include <gtest/gtest.h>
#include <sys/stat.h>
#include <sysprop/sysprop.h>

// Integration tests exercise PropertyStore + FileBackend directly rather than
// the global singleton (which is initialised once via call_once and cannot be
// reset between tests).
#include "file_backend.h"
#include "property_store.h"

using sysprop::internal::FileBackend;
using sysprop::internal::PropertyStore;

// ── Fixture ───────────────────────────────────────────────────────────────────

class IntegrationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    char rt[] = "/tmp/sysprop_it_rt_XXXXXX";
    char ps[] = "/tmp/sysprop_it_ps_XXXXXX";
    ASSERT_NE(::mkdtemp(rt), nullptr);
    ASSERT_NE(::mkdtemp(ps), nullptr);
    rt_dir_ = rt;
    ps_dir_ = ps;

    rt_backend_ = std::make_unique<FileBackend>(rt_dir_);
    ps_backend_ = std::make_unique<FileBackend>(ps_dir_);
    store_ = std::make_unique<PropertyStore>(rt_backend_.get(), ps_backend_.get());
  }

  void TearDown() override {
    store_.reset();
    rt_backend_.reset();
    ps_backend_.reset();
    const auto rm = [](const std::string& d) { (void)::system(("rm -rf " + d).c_str()); };
    rm(rt_dir_);
    rm(ps_dir_);
  }

  std::string rt_dir_;
  std::string ps_dir_;
  std::unique_ptr<FileBackend> rt_backend_;
  std::unique_ptr<FileBackend> ps_backend_;
  std::unique_ptr<PropertyStore> store_;
  char buf_[SYSPROP_MAX_VALUE_LENGTH] = {};
};

// ── Core Set / Get / Delete ───────────────────────────────────────────────────

TEST_F(IntegrationTest, SetGetRoundTrip) {
  ASSERT_EQ(SYSPROP_OK, store_->Set("net.hostname", "embedded-box"));
  const int n = store_->Get("net.hostname", buf_, sizeof(buf_));
  ASSERT_GE(n, 0);
  EXPECT_STREQ("embedded-box", buf_);
}

TEST_F(IntegrationTest, DeleteRemovesProperty) {
  ASSERT_EQ(SYSPROP_OK, store_->Set("tmp.key", "v"));
  ASSERT_EQ(SYSPROP_OK, store_->Delete("tmp.key"));
  EXPECT_EQ(SYSPROP_ERR_NOT_FOUND, store_->Get("tmp.key", buf_, sizeof(buf_)));
}

TEST_F(IntegrationTest, GetNotFoundReturnsError) {
  EXPECT_EQ(SYSPROP_ERR_NOT_FOUND, store_->Get("no.such.key", buf_, sizeof(buf_)));
}

// ── ro.* properties ───────────────────────────────────────────────────────────

TEST_F(IntegrationTest, RoCanBeSetOnce) {
  EXPECT_EQ(SYSPROP_OK, store_->Set("ro.board.platform", "armv8"));
  const int n = store_->Get("ro.board.platform", buf_, sizeof(buf_));
  ASSERT_GE(n, 0);
  EXPECT_STREQ("armv8", buf_);
}

TEST_F(IntegrationTest, RoCannotBeOverwritten) {
  ASSERT_EQ(SYSPROP_OK, store_->Set("ro.board.platform", "armv8"));
  EXPECT_EQ(SYSPROP_ERR_READ_ONLY, store_->Set("ro.board.platform", "x86"));

  // Original value must be unchanged.
  const int n = store_->Get("ro.board.platform", buf_, sizeof(buf_));
  ASSERT_GE(n, 0);
  EXPECT_STREQ("armv8", buf_);
}

TEST_F(IntegrationTest, RoCannotBeDeleted) {
  ASSERT_EQ(SYSPROP_OK, store_->Set("ro.my.prop", "value"));
  EXPECT_EQ(SYSPROP_ERR_READ_ONLY, store_->Delete("ro.my.prop"));
}

// ── persist.* properties ─────────────────────────────────────────────────────

TEST_F(IntegrationTest, PersistPropertyWrittenToRuntimeAndDisk) {
  ASSERT_EQ(SYSPROP_OK, store_->Set("persist.wifi.ssid", "MyNet"));

  const int n = store_->Get("persist.wifi.ssid", buf_, sizeof(buf_));
  ASSERT_GE(n, 0);
  EXPECT_STREQ("MyNet", buf_);

  // Verify the persistent backing file exists.
  char ps_path[512];
  std::snprintf(ps_path, sizeof(ps_path), "%s/persist.wifi.ssid", ps_dir_.c_str());
  EXPECT_EQ(0, ::access(ps_path, F_OK)) << "Persistent file not found: " << ps_path;
}

TEST_F(IntegrationTest, LoadPersistentPropertiesRestoresAfterReboot) {
  ASSERT_EQ(SYSPROP_OK, store_->Set("persist.sys.timezone", "UTC"));

  // Simulate reboot: fresh runtime dir, same persistent dir.
  char new_rt[] = "/tmp/sysprop_it_newrt_XXXXXX";
  ASSERT_NE(::mkdtemp(new_rt), nullptr);
  const std::string new_rt_dir{new_rt};

  {
    FileBackend new_runtime{new_rt_dir};
    PropertyStore new_store{&new_runtime, ps_backend_.get()};

    EXPECT_EQ(SYSPROP_ERR_NOT_FOUND, new_store.Get("persist.sys.timezone", buf_, sizeof(buf_)));

    EXPECT_GT(new_store.LoadPersistentProperties(), 0);

    const int n = new_store.Get("persist.sys.timezone", buf_, sizeof(buf_));
    ASSERT_GE(n, 0);
    EXPECT_STREQ("UTC", buf_);
  }

  (void)::system(("rm -rf " + new_rt_dir).c_str());
}

// ── ForEach ───────────────────────────────────────────────────────────────────

TEST_F(IntegrationTest, ForEachVisitsAllProperties) {
  ASSERT_EQ(SYSPROP_OK, store_->Set("a.x", "1"));
  ASSERT_EQ(SYSPROP_OK, store_->Set("b.y", "2"));
  ASSERT_EQ(SYSPROP_OK, store_->Set("c.z", "3"));

  int count = 0;
  (void)store_->ForEach([&](const char*, const char*) {
    ++count;
    return true;
  });
  EXPECT_EQ(3, count);
}

// ── Typed helpers via the global singleton ────────────────────────────────────
// These tests hit the global singleton; they only verify the default-value path
// to avoid coupling to the runtime directory state on the test host.

TEST(TypedHelpers, GetIntReturnsDefault) { EXPECT_EQ(42, sysprop_get_int("no.such.int.key", 42)); }

TEST(TypedHelpers, GetBoolReturnsDefault) {
  EXPECT_TRUE(sysprop_get_bool("no.such.bool.key", 1));
  EXPECT_FALSE(sysprop_get_bool("no.such.bool.key", 0));
}

TEST(TypedHelpers, GetFloatReturnsDefault) {
  EXPECT_FLOAT_EQ(3.14f, sysprop_get_float("no.such.float.key", 3.14f));
}
