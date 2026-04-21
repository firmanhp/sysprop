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
    std::string rt = testing::TempDir() + "sysprop_it_rt_XXXXXX";
    std::string ps = testing::TempDir() + "sysprop_it_ps_XXXXXX";
    ASSERT_NE(::mkdtemp(rt.data()), nullptr);
    ASSERT_NE(::mkdtemp(ps.data()), nullptr);
    rt_dir_ = rt;
    ps_dir_ = ps;

    rt_backend_ = std::make_unique<FileBackend>(rt_dir_.c_str());
    ps_backend_ = std::make_unique<FileBackend>(ps_dir_.c_str());
    store_ = std::make_unique<PropertyStore>(rt_backend_.get(), ps_backend_.get());
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

TEST_F(IntegrationTest, PersistPropertyWrittenToDisk) {
  ASSERT_EQ(SYSPROP_OK, store_->Set("persist.wifi.ssid", "MyNet"));

  const int n = store_->Get("persist.wifi.ssid", buf_, sizeof(buf_));
  ASSERT_GE(n, 0);
  EXPECT_STREQ("MyNet", buf_);

  // Verify only the persistent backing file exists (not runtime).
  char ps_path[512];
  std::snprintf(ps_path, sizeof(ps_path), "%s/persist.wifi.ssid", ps_dir_.c_str());
  EXPECT_EQ(0, ::access(ps_path, F_OK)) << "Persistent file not found: " << ps_path;
  char rt_path[512];
  std::snprintf(rt_path, sizeof(rt_path), "%s/persist.wifi.ssid", rt_dir_.c_str());
  EXPECT_NE(0, ::access(rt_path, F_OK)) << "Unexpected runtime file found: " << rt_path;
}

TEST_F(IntegrationTest, PersistPropertiesAccessibleAfterReboot) {
  ASSERT_EQ(SYSPROP_OK, store_->Set("persist.sys.timezone", "UTC"));

  // Simulate reboot: fresh runtime dir, same persistent dir.
  std::string new_rt = testing::TempDir() + "sysprop_it_newrt_XXXXXX";
  ASSERT_NE(::mkdtemp(new_rt.data()), nullptr);
  const std::string new_rt_dir{new_rt};

  {
    FileBackend new_runtime{new_rt_dir.c_str()};
    PropertyStore new_store{&new_runtime, ps_backend_.get()};

    // persist.* reads directly from persistent_ — no LoadPersistentProperties needed.
    const int n = new_store.Get("persist.sys.timezone", buf_, sizeof(buf_));
    ASSERT_GE(n, 0);
    EXPECT_STREQ("UTC", buf_);
  }
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

// ── ForEach early stop ────────────────────────────────────────────────────────

TEST_F(IntegrationTest, ForEachEarlyStop) {
  ASSERT_EQ(SYSPROP_OK, store_->Set("a.x", "1"));
  ASSERT_EQ(SYSPROP_OK, store_->Set("b.y", "2"));
  ASSERT_EQ(SYSPROP_OK, store_->Set("c.z", "3"));

  int count = 0;
  (void)store_->ForEach([&](const char*, const char*) {
    ++count;
    return false;
  });
  EXPECT_EQ(1, count);
}

// ── Multiple persist props survive reboot ─────────────────────────────────────

TEST_F(IntegrationTest, MultiplePersistPropsAccessibleAfterReboot) {
  ASSERT_EQ(SYSPROP_OK, store_->Set("persist.a", "1"));
  ASSERT_EQ(SYSPROP_OK, store_->Set("persist.b", "2"));
  ASSERT_EQ(SYSPROP_OK, store_->Set("persist.c", "3"));

  std::string new_rt = testing::TempDir() + "sysprop_it_multi_XXXXXX";
  ASSERT_NE(::mkdtemp(new_rt.data()), nullptr);
  const std::string new_rt_dir{new_rt};

  {
    FileBackend new_runtime{new_rt_dir.c_str()};
    PropertyStore new_store{&new_runtime, ps_backend_.get()};

    for (const char* key : {"persist.a", "persist.b", "persist.c"}) {
      const int n = new_store.Get(key, buf_, sizeof(buf_));
      EXPECT_GE(n, 0) << key;
    }
  }
}

// ── Evil-attacker scenarios ───────────────────────────────────────────────────

TEST_F(IntegrationTest, ShellMetacharacterKeysRejected) {
  for (const char* key :
       {"key|pipe", "key;semi", "key&bg", "key$(sub)", "key`cmd`", "key>out", "key<in"}) {
    EXPECT_NE(SYSPROP_OK, store_->Set(key, "evil")) << "key=" << key;
    EXPECT_NE(SYSPROP_OK, store_->Get(key, buf_, sizeof(buf_))) << "key=" << key;
  }
}

TEST_F(IntegrationTest, PathTraversalKeysRejectedAndNoEscape) {
  for (const char* key : {"../escape", "../../etc/passwd", "a/../b", "/absolute"}) {
    EXPECT_NE(SYSPROP_OK, store_->Set(key, "evil")) << "key=" << key;
  }
  // No file must have escaped into the parent of the runtime dir.
  EXPECT_EQ(SYSPROP_ERR_NOT_FOUND, store_->Get("escape", buf_, sizeof(buf_)));
  EXPECT_EQ(SYSPROP_ERR_NOT_FOUND, store_->Get("passwd", buf_, sizeof(buf_)));
}

TEST_F(IntegrationTest, ValueUrlSpecialCharsRoundTrip) {
  // URL characters that are valid values but invalid key chars must survive.
  const char* url = "https://example.com/path?q=1&foo=bar#frag";
  ASSERT_EQ(SYSPROP_OK, store_->Set("net.update.url", url));
  ASSERT_GE(store_->Get("net.update.url", buf_, sizeof(buf_)), 0);
  EXPECT_STREQ(url, buf_);
}

TEST_F(IntegrationTest, ValueLeadingTrailingSpacesPreserved) {
  ASSERT_EQ(SYSPROP_OK, store_->Set("sys.banner", "  padded  "));
  ASSERT_GE(store_->Get("sys.banner", buf_, sizeof(buf_)), 0);
  EXPECT_STREQ("  padded  ", buf_);
}

TEST_F(IntegrationTest, ValueEqualSignsNotReparsed) {
  // Value that looks like key=value pairs must not be mangled.
  ASSERT_EQ(SYSPROP_OK, store_->Set("sys.cmdline", "root=/dev/sda ro quiet"));
  ASSERT_GE(store_->Get("sys.cmdline", buf_, sizeof(buf_)), 0);
  EXPECT_STREQ("root=/dev/sda ro quiet", buf_);
}

TEST_F(IntegrationTest, FailedSetValueTooLongLeavesNoGhostKey) {
  const std::string too_long(SYSPROP_MAX_VALUE_LENGTH, 'x');
  ASSERT_NE(SYSPROP_OK, store_->Set("ghost.key", too_long.c_str()));
  EXPECT_EQ(SYSPROP_ERR_NOT_FOUND, store_->Get("ghost.key", buf_, sizeof(buf_)));
}

TEST_F(IntegrationTest, FailedSetInvalidKeyLeavesNoAdjacentKeys) {
  ASSERT_NE(SYSPROP_OK, store_->Set("bad..key", "val"));
  EXPECT_EQ(SYSPROP_ERR_INVALID_KEY, store_->Get("bad..key", buf_, sizeof(buf_)));
  EXPECT_EQ(SYSPROP_ERR_NOT_FOUND, store_->Get("bad", buf_, sizeof(buf_)));
  EXPECT_EQ(SYSPROP_ERR_NOT_FOUND, store_->Get("key", buf_, sizeof(buf_)));
}

TEST_F(IntegrationTest, GetOneByteBufNullTerminatedNoOverflow) {
  ASSERT_EQ(SYSPROP_OK, store_->Set("trunc.key", "longvalue"));
  char tiny[1] = {'\x7f'};
  ASSERT_GE(store_->Get("trunc.key", tiny, sizeof(tiny)), 0);
  EXPECT_EQ('\0', tiny[0]);
}

TEST_F(IntegrationTest, OverwriteLargeValueWithSmallNoStaleBytes) {
  const std::string big(SYSPROP_MAX_VALUE_LENGTH - 1, 'z');
  ASSERT_EQ(SYSPROP_OK, store_->Set("shrink.key", big.c_str()));
  ASSERT_EQ(SYSPROP_OK, store_->Set("shrink.key", "small"));
  const int n = store_->Get("shrink.key", buf_, sizeof(buf_));
  ASSERT_EQ(5, n);
  EXPECT_STREQ("small", buf_);
}

TEST_F(IntegrationTest, RoPrefixTakesPrecedenceOverPersistPrefix) {
  // ro.persist.* is read-only and must NOT be written to the persistent backend.
  ASSERT_EQ(SYSPROP_OK, store_->Set("ro.persist.cfg", "locked"));
  EXPECT_EQ(SYSPROP_ERR_READ_ONLY, store_->Set("ro.persist.cfg", "hacked"));
  char ps_path[512];
  std::snprintf(ps_path, sizeof(ps_path), "%s/ro.persist.cfg",
                ps_dir_.c_str());  // NOLINT(cppcoreguidelines-pro-type-vararg)
  EXPECT_NE(0, ::access(ps_path, F_OK));
}

TEST_F(IntegrationTest, TwoStoresOnSameRuntimeDirRoFirstWins) {
  // Simulate two processes racing to set the same ro.* key.
  // Whichever commits first locks it; the other must get READ_ONLY.
  FileBackend shared_rt{rt_dir_.c_str()};
  FileBackend shared_ps{ps_dir_.c_str()};
  PropertyStore store2{&shared_rt, &shared_ps};

  ASSERT_EQ(SYSPROP_OK, store_->Set("ro.race.key", "first"));
  EXPECT_EQ(SYSPROP_ERR_READ_ONLY, store2.Set("ro.race.key", "second"));

  const int n = store_->Get("ro.race.key", buf_, sizeof(buf_));
  ASSERT_GE(n, 0);
  EXPECT_STREQ("first", buf_);
}

// ── sysprop_error_string ──────────────────────────────────────────────────────

TEST(ErrorString, CoversAllCodes) {
  EXPECT_STREQ("ok", sysprop_error_string(SYSPROP_OK));
  EXPECT_STREQ("not found", sysprop_error_string(SYSPROP_ERR_NOT_FOUND));
  EXPECT_STREQ("read-only property", sysprop_error_string(SYSPROP_ERR_READ_ONLY));
  EXPECT_STREQ("invalid key", sysprop_error_string(SYSPROP_ERR_INVALID_KEY));
  EXPECT_STREQ("value too long", sysprop_error_string(SYSPROP_ERR_VALUE_TOO_LONG));
  EXPECT_STREQ("key too long", sysprop_error_string(SYSPROP_ERR_KEY_TOO_LONG));
  EXPECT_STREQ("I/O error", sysprop_error_string(SYSPROP_ERR_IO));
  EXPECT_STREQ("permission denied", sysprop_error_string(SYSPROP_ERR_PERMISSION));
  EXPECT_STREQ("not initialized", sysprop_error_string(SYSPROP_ERR_NOT_INITIALIZED));
  EXPECT_STREQ("buffer too small", sysprop_error_string(SYSPROP_ERR_BUFFER_TOO_SMALL));
  EXPECT_STREQ("unknown error", sysprop_error_string(-999));
}

// ── Global C API via singleton ────────────────────────────────────────────────
//
// GlobalApiTest initializes the singleton once (SetUpTestSuite) into temp dirs
// and exercises the full public C API including the typed-helper parsing paths.
// Keys are unique per test to avoid cross-test interference.

class GlobalApiTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    std::string rt = testing::TempDir() + "sysprop_gapi_rt_XXXXXX";
    std::string ps = testing::TempDir() + "sysprop_gapi_ps_XXXXXX";
    rt_dir_ = ::mkdtemp(rt.data());
    ps_dir_ = ::mkdtemp(ps.data());
    sysprop_config_t cfg{rt_dir_.c_str(), ps_dir_.c_str(), 1};
    (void)sysprop_init(&cfg);
  }

  static std::string rt_dir_;
  static std::string ps_dir_;
  char buf_[SYSPROP_MAX_VALUE_LENGTH] = {};
};

std::string GlobalApiTest::rt_dir_;
std::string GlobalApiTest::ps_dir_;

TEST_F(GlobalApiTest, SetAndGet) {
  ASSERT_EQ(SYSPROP_OK, sysprop_set("gapi.hello", "world"));
  ASSERT_GE(sysprop_get("gapi.hello", buf_, sizeof(buf_)), 0);
  EXPECT_STREQ("world", buf_);
}

TEST_F(GlobalApiTest, DeleteRemovesKey) {
  ASSERT_EQ(SYSPROP_OK, sysprop_set("gapi.del", "v"));
  ASSERT_EQ(SYSPROP_OK, sysprop_delete("gapi.del"));
  EXPECT_EQ(SYSPROP_ERR_NOT_FOUND, sysprop_get("gapi.del", buf_, sizeof(buf_)));
}

TEST_F(GlobalApiTest, InitIsIdempotent) {
  ASSERT_EQ(SYSPROP_OK, sysprop_set("gapi.idempotent", "yes"));

  // Second init with different dirs must be silently ignored.
  const std::string tmp = testing::TempDir();
  sysprop_config_t other{tmp.c_str(), tmp.c_str(), 0};
  EXPECT_EQ(SYSPROP_OK, sysprop_init(&other));

  // Key set before second init must still be reachable (store not replaced).
  ASSERT_GE(sysprop_get("gapi.idempotent", buf_, sizeof(buf_)), 0);
  EXPECT_STREQ("yes", buf_);
}

TEST_F(GlobalApiTest, GetWithBufLen1ReturnsEmpty) {
  ASSERT_EQ(SYSPROP_OK, sysprop_set("gapi.bufone", "hello"));
  char tiny[1] = {};
  const int n = sysprop_get("gapi.bufone", tiny, sizeof(tiny));
  ASSERT_GE(n, 0);
  EXPECT_EQ('\0', tiny[0]);
}

TEST_F(GlobalApiTest, GetIntParsesPositive) {
  ASSERT_EQ(SYSPROP_OK, sysprop_set("gapi.int.pos", "42"));
  EXPECT_EQ(42, sysprop_get_int("gapi.int.pos", 0));
}

TEST_F(GlobalApiTest, GetIntParsesNegative) {
  ASSERT_EQ(SYSPROP_OK, sysprop_set("gapi.int.neg", "-7"));
  EXPECT_EQ(-7, sysprop_get_int("gapi.int.neg", 0));
}

TEST_F(GlobalApiTest, GetIntParsesZero) {
  ASSERT_EQ(SYSPROP_OK, sysprop_set("gapi.int.zero", "0"));
  EXPECT_EQ(0, sysprop_get_int("gapi.int.zero", 99));
}

TEST_F(GlobalApiTest, GetIntReturnsDefaultForNonNumeric) {
  ASSERT_EQ(SYSPROP_OK, sysprop_set("gapi.int.nan", "abc"));
  EXPECT_EQ(99, sysprop_get_int("gapi.int.nan", 99));
}

TEST_F(GlobalApiTest, GetBoolRecognizesTrueStrings) {
  for (const char* val : {"1", "true", "yes", "on"}) {
    const std::string key = std::string("gapi.bool.t.") + val;
    ASSERT_EQ(SYSPROP_OK, sysprop_set(key.c_str(), val));
    EXPECT_TRUE(sysprop_get_bool(key.c_str(), 0)) << "val=" << val;
  }
}

TEST_F(GlobalApiTest, GetBoolRecognizesFalseStrings) {
  for (const char* val : {"0", "false", "no", "off"}) {
    const std::string key = std::string("gapi.bool.f.") + val;
    ASSERT_EQ(SYSPROP_OK, sysprop_set(key.c_str(), val));
    EXPECT_FALSE(sysprop_get_bool(key.c_str(), 1)) << "val=" << val;
  }
}

TEST_F(GlobalApiTest, GetBoolReturnsDefaultForUnrecognized) {
  ASSERT_EQ(SYSPROP_OK, sysprop_set("gapi.bool.unk", "maybe"));
  EXPECT_TRUE(sysprop_get_bool("gapi.bool.unk", 1));
  EXPECT_FALSE(sysprop_get_bool("gapi.bool.unk", 0));
}

TEST_F(GlobalApiTest, GetFloatParsesValue) {
  ASSERT_EQ(SYSPROP_OK, sysprop_set("gapi.float", "3.14"));
  EXPECT_FLOAT_EQ(3.14f, sysprop_get_float("gapi.float", 0.0f));
}

TEST_F(GlobalApiTest, GetFloatParsesNegative) {
  ASSERT_EQ(SYSPROP_OK, sysprop_set("gapi.float.neg", "-1.5"));
  EXPECT_FLOAT_EQ(-1.5f, sysprop_get_float("gapi.float.neg", 0.0f));
}

TEST_F(GlobalApiTest, GetFloatReturnsDefaultForNonNumeric) {
  ASSERT_EQ(SYSPROP_OK, sysprop_set("gapi.float.nan", "xyz"));
  EXPECT_FLOAT_EQ(2.0f, sysprop_get_float("gapi.float.nan", 2.0f));
}

// ── Typed helpers via the global singleton ────────────────────────────────────
// These tests verify the default-value path when the key does not exist.

TEST(TypedHelpers, GetIntReturnsDefault) { EXPECT_EQ(42, sysprop_get_int("no.such.int.key", 42)); }

TEST(TypedHelpers, GetBoolReturnsDefault) {
  EXPECT_TRUE(sysprop_get_bool("no.such.bool.key", 1));
  EXPECT_FALSE(sysprop_get_bool("no.such.bool.key", 0));
}

TEST(TypedHelpers, GetFloatReturnsDefault) {
  EXPECT_FLOAT_EQ(3.14f, sysprop_get_float("no.such.float.key", 3.14f));
}

// ── Typed helper parsing edge cases (singleton must be initialized) ───────────

// "42abc" → strtol succeeds (end != buf, errno==0) → returns 42, not default
TEST_F(GlobalApiTest, GetIntPartialNumericReturnsParsedPrefix) {
  ASSERT_EQ(SYSPROP_OK, sysprop_set("gapi.int.partial", "42abc"));
  EXPECT_EQ(42, sysprop_get_int("gapi.int.partial", 0));
}

// Very large value → strtol sets errno=ERANGE → returns default
TEST_F(GlobalApiTest, GetIntOverflowReturnsDefault) {
  ASSERT_EQ(SYSPROP_OK, sysprop_set("gapi.int.overflow", "99999999999999999999"));
  EXPECT_EQ(-1, sysprop_get_int("gapi.int.overflow", -1));
}

// Very negative value → strtol sets errno=ERANGE → returns default
TEST_F(GlobalApiTest, GetIntNegativeOverflowReturnsDefault) {
  ASSERT_EQ(SYSPROP_OK, sysprop_set("gapi.int.negoverflow", "-99999999999999999999"));
  EXPECT_EQ(-1, sysprop_get_int("gapi.int.negoverflow", -1));
}

// "True" (capital T) — implementation is case-sensitive; falls through to default
TEST_F(GlobalApiTest, GetBoolCapitalTrueReturnsDefault) {
  ASSERT_EQ(SYSPROP_OK, sysprop_set("gapi.bool.captrue", "True"));
  EXPECT_EQ(0, sysprop_get_bool("gapi.bool.captrue", 0));
  EXPECT_EQ(1, sysprop_get_bool("gapi.bool.captrue", 1));
}

// "TRUE" — uppercase also falls through to default
TEST_F(GlobalApiTest, GetBoolAllCapsReturnsDefault) {
  ASSERT_EQ(SYSPROP_OK, sysprop_set("gapi.bool.allcaps", "TRUE"));
  EXPECT_EQ(0, sysprop_get_bool("gapi.bool.allcaps", 0));
}

// "0.0" must return 0.0f, not default_value
TEST_F(GlobalApiTest, GetFloatZeroReturnsZero) {
  ASSERT_EQ(SYSPROP_OK, sysprop_set("gapi.float.zero", "0.0"));
  EXPECT_FLOAT_EQ(0.0f, sysprop_get_float("gapi.float.zero", 99.0f));
}

// Integer string "5" parsed by strtof → 5.0f
TEST_F(GlobalApiTest, GetFloatIntegerInputReturnsFloat) {
  ASSERT_EQ(SYSPROP_OK, sysprop_set("gapi.float.int", "5"));
  EXPECT_FLOAT_EQ(5.0f, sysprop_get_float("gapi.float.int", 0.0f));
}

// Scientific notation "1.5e3" → 1500.0f
TEST_F(GlobalApiTest, GetFloatScientificNotationParsed) {
  ASSERT_EQ(SYSPROP_OK, sysprop_set("gapi.float.sci", "1.5e3"));
  EXPECT_FLOAT_EQ(1500.0f, sysprop_get_float("gapi.float.sci", 0.0f));
}

// sysprop_get with buf_len=0 via the public C API → SYSPROP_ERR_BUFFER_TOO_SMALL
TEST_F(GlobalApiTest, GetWithBufLenZeroReturnsBufferTooSmall) {
  ASSERT_EQ(SYSPROP_OK, sysprop_set("gapi.bufzero", "hello"));
  char dummy = '\x7f';
  EXPECT_EQ(SYSPROP_ERR_BUFFER_TOO_SMALL, sysprop_get("gapi.bufzero", &dummy, 0));
}

// strtol skips leading whitespace → " 42" parses as 42, not default
TEST_F(GlobalApiTest, GetIntLeadingWhitespaceIsParsed) {
  ASSERT_EQ(SYSPROP_OK, sysprop_set("gapi.int.ws", " 42"));
  EXPECT_EQ(42, sysprop_get_int("gapi.int.ws", 0));
}

// string_view comparison is exact → " true" does not match "true", returns default
TEST_F(GlobalApiTest, GetBoolLeadingWhitespaceReturnsDefault) {
  ASSERT_EQ(SYSPROP_OK, sysprop_set("gapi.bool.ws", " true"));
  EXPECT_EQ(0, sysprop_get_bool("gapi.bool.ws", 0));
  EXPECT_EQ(1, sysprop_get_bool("gapi.bool.ws", 1));
}

// ── Uninitialized C API ───────────────────────────────────────────────────────
//
// These tests intentionally do NOT call sysprop_init(). Because ctest runs each
// test via --gtest_filter in its own process, the placement-new singleton is
// never constructed and GetStore() returns nullptr.

TEST(UninitializedApi, GetReturnsNotInitialized) {
  char buf[SYSPROP_MAX_VALUE_LENGTH];
  EXPECT_EQ(SYSPROP_ERR_NOT_INITIALIZED, sysprop_get("any.key", buf, sizeof(buf)));
}

TEST(UninitializedApi, SetReturnsNotInitialized) {
  EXPECT_EQ(SYSPROP_ERR_NOT_INITIALIZED, sysprop_set("any.key", "value"));
}

TEST(UninitializedApi, DeleteReturnsNotInitialized) {
  EXPECT_EQ(SYSPROP_ERR_NOT_INITIALIZED, sysprop_delete("any.key"));
}

// sysprop_init(nullptr) uses compiled-in defaults; must not crash or fail.
TEST(UninitializedApi, InitWithNullptrSucceeds) { EXPECT_EQ(SYSPROP_OK, sysprop_init(nullptr)); }

// ── C API: delete nonexistent key ─────────────────────────────────────────────

TEST_F(GlobalApiTest, DeleteNonexistentKeyReturnsNotFound) {
  EXPECT_EQ(SYSPROP_ERR_NOT_FOUND, sysprop_delete("no.such.delete.key"));
}

// ── C++ std::string overload ──────────────────────────────────────────────────

TEST_F(GlobalApiTest, CppGetReturnsValue) {
  ASSERT_EQ(SYSPROP_OK, sysprop_set("cpp.get.value", "hello"));
  EXPECT_EQ("hello", sysprop_get("cpp.get.value", std::string("default")));
}

TEST_F(GlobalApiTest, CppGetReturnsDefaultWhenMissing) {
  EXPECT_EQ("fallback", sysprop_get("cpp.get.missing", std::string("fallback")));
}

TEST_F(GlobalApiTest, CppGetReturnsDefaultWhenUninitialized) {
  // The overload must propagate the error from the C layer and return the default.
  // (Singleton is initialized in this suite, so exercise via an invalid key instead.)
  EXPECT_EQ("def", sysprop_get("", std::string("def")));
}
