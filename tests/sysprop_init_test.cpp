#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include <gtest/gtest.h>
#include <sys/stat.h>
#include <sysprop/sysprop.h>

#include "defaults_loader.h"
#include "file_backend.h"
#include "file_property_store.h"

using sysprop::internal::FileBackend;
using sysprop::internal::FilePropertyStore;
using sysprop::tools::LoadDefaultsFile;

// ── Fixture ───────────────────────────────────────────────────────────────────

class DefaultsLoaderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    std::string rt = testing::TempDir() + "sysprop_dlt_rt_XXXXXX";
    std::string ps = testing::TempDir() + "sysprop_dlt_ps_XXXXXX";
    ASSERT_NE(::mkdtemp(rt.data()), nullptr) << strerror(errno);
    ASSERT_NE(::mkdtemp(ps.data()), nullptr) << strerror(errno);
    rt_dir_ = rt;
    ps_dir_ = ps;
    rt_backend_ = std::make_unique<FileBackend>(rt_dir_.c_str());
    ps_backend_ = std::make_unique<FileBackend>(ps_dir_.c_str());
    store_ = std::make_unique<FilePropertyStore>(rt_backend_.get(), ps_backend_.get());
  }

  // Write content to a uniquely-named temp file and return its path.
  std::string WritePropFile(const char* content) {
    std::string tmpl = testing::TempDir() + "sysprop_prop_XXXXXX";
    const int fd = ::mkstemp(tmpl.data());
    EXPECT_GE(fd, 0) << strerror(errno);
    const std::size_t len = std::strlen(content);
    EXPECT_EQ(static_cast<ssize_t>(len), ::write(fd, content, len));
    ::close(fd);
    return tmpl;
  }

  // Read a property from the runtime store.
  std::string Get(const char* key) {
    char buf[SYSPROP_MAX_VALUE_LENGTH] = {};
    (void)store_->Get(key, buf, sizeof(buf));
    return buf;
  }

  bool FileExists(const std::string& dir, const char* key) {
    return ::access((dir + "/" + key).c_str(), F_OK) == 0;
  }

  std::string rt_dir_;
  std::string ps_dir_;
  std::unique_ptr<FileBackend> rt_backend_;
  std::unique_ptr<FileBackend> ps_backend_;
  std::unique_ptr<FilePropertyStore> store_;
};

// ── Basic parsing ─────────────────────────────────────────────────────────────

TEST_F(DefaultsLoaderTest, SingleKeyValueParsed) {
  const auto f = WritePropFile("net.hostname=my-board\n");
  EXPECT_EQ(1, LoadDefaultsFile(f.c_str(), *store_));
  EXPECT_EQ("my-board", Get("net.hostname"));
}

TEST_F(DefaultsLoaderTest, MultiplePropertiesReturnCorrectCount) {
  const auto f = WritePropFile(
      "ro.build.version=1.0\n"
      "ro.hw.board=armv8\n"
      "net.hostname=box\n");
  EXPECT_EQ(3, LoadDefaultsFile(f.c_str(), *store_));
  EXPECT_EQ("1.0", Get("ro.build.version"));
  EXPECT_EQ("armv8", Get("ro.hw.board"));
  EXPECT_EQ("box", Get("net.hostname"));
}

TEST_F(DefaultsLoaderTest, EmptyFileReturnsZero) {
  const auto f = WritePropFile("");
  EXPECT_EQ(0, LoadDefaultsFile(f.c_str(), *store_));
}

// ── Whitespace and comment handling ───────────────────────────────────────────

TEST_F(DefaultsLoaderTest, CommentsSkipped) {
  const auto f = WritePropFile(
      "# this is a comment\n"
      "net.hostname=box\n"
      "# another comment\n");
  EXPECT_EQ(1, LoadDefaultsFile(f.c_str(), *store_));
  EXPECT_EQ("box", Get("net.hostname"));
}

TEST_F(DefaultsLoaderTest, BlankLinesSkipped) {
  const auto f = WritePropFile(
      "\n"
      "net.hostname=box\n"
      "\n"
      "\n");
  EXPECT_EQ(1, LoadDefaultsFile(f.c_str(), *store_));
}

TEST_F(DefaultsLoaderTest, LeadingWhitespaceStripped) {
  const auto f = WritePropFile(
      "   net.hostname=box\n"
      "\t\tro.board=arm\n");
  EXPECT_EQ(2, LoadDefaultsFile(f.c_str(), *store_));
  EXPECT_EQ("box", Get("net.hostname"));
  EXPECT_EQ("arm", Get("ro.board"));
}

TEST_F(DefaultsLoaderTest, WhitespaceOnlyLineSkipped) {
  const auto f = WritePropFile(
      "   \n"
      "net.hostname=box\n"
      "\t\n");
  EXPECT_EQ(1, LoadDefaultsFile(f.c_str(), *store_));
}

TEST_F(DefaultsLoaderTest, CommentAfterLeadingWhitespaceSkipped) {
  const auto f = WritePropFile(
      "    # indented comment\n"
      "net.hostname=box\n");
  EXPECT_EQ(1, LoadDefaultsFile(f.c_str(), *store_));
  EXPECT_EQ("box", Get("net.hostname"));
}

// ── Line-ending handling ──────────────────────────────────────────────────────

TEST_F(DefaultsLoaderTest, CrlfLineEndingsHandled) {
  const auto f = WritePropFile("net.hostname=box\r\nro.board=arm\r\n");
  EXPECT_EQ(2, LoadDefaultsFile(f.c_str(), *store_));
  EXPECT_EQ("box", Get("net.hostname"));
  EXPECT_EQ("arm", Get("ro.board"));
}

TEST_F(DefaultsLoaderTest, LineWithNoTrailingNewline) {
  const auto f = WritePropFile("net.hostname=box");  // no trailing newline
  EXPECT_EQ(1, LoadDefaultsFile(f.c_str(), *store_));
  EXPECT_EQ("box", Get("net.hostname"));
}

// ── Value parsing edge cases ──────────────────────────────────────────────────

TEST_F(DefaultsLoaderTest, ValueCanContainEquals) {
  // Only the first '=' splits key from value.
  const auto f = WritePropFile("net.update.url=https://example.com/path?a=1&b=2\n");
  EXPECT_EQ(1, LoadDefaultsFile(f.c_str(), *store_));
  EXPECT_EQ("https://example.com/path?a=1&b=2", Get("net.update.url"));
}

TEST_F(DefaultsLoaderTest, EmptyValueAllowed) {
  const auto f = WritePropFile("net.proxy=\n");
  EXPECT_EQ(1, LoadDefaultsFile(f.c_str(), *store_));
  EXPECT_EQ("", Get("net.proxy"));
}

TEST_F(DefaultsLoaderTest, ValueWithSpacesPreserved) {
  const auto f = WritePropFile("sys.motd=Hello Embedded World\n");
  EXPECT_EQ(1, LoadDefaultsFile(f.c_str(), *store_));
  EXPECT_EQ("Hello Embedded World", Get("sys.motd"));
}

// ── Malformed-line handling ───────────────────────────────────────────────────

TEST_F(DefaultsLoaderTest, MalformedLineWithoutEqualIsSkipped) {
  // A line with no '=' should be skipped; parsing continues on the next line.
  const auto f = WritePropFile(
      "this_has_no_equals\n"
      "net.hostname=box\n");
  EXPECT_EQ(1, LoadDefaultsFile(f.c_str(), *store_));
  EXPECT_EQ("box", Get("net.hostname"));
}

TEST_F(DefaultsLoaderTest, MixOfValidAndMalformedLinesCountsOnlyValid) {
  const auto f = WritePropFile(
      "ro.build.version=1.0\n"
      "MALFORMED\n"
      "ro.hw.board=arm\n"
      "ALSO_MALFORMED\n"
      "net.hostname=box\n");
  EXPECT_EQ(3, LoadDefaultsFile(f.c_str(), *store_));
}

// ── File access errors ────────────────────────────────────────────────────────

TEST_F(DefaultsLoaderTest, NonexistentFileReturnsMinusOne) {
  EXPECT_EQ(-1, LoadDefaultsFile("/nonexistent/path/build.prop", *store_));
}

TEST_F(DefaultsLoaderTest, UnreadableFileReturnsMinusOne) {
  if (::getuid() == 0) {
    GTEST_SKIP() << "root bypasses permission checks";
  }
  const auto f = WritePropFile("net.hostname=box\n");
  ASSERT_EQ(0, ::chmod(f.c_str(), 0000));
  EXPECT_EQ(-1, LoadDefaultsFile(f.c_str(), *store_));
  ::chmod(f.c_str(), 0644);
}

// ── Key/value validation ──────────────────────────────────────────────────────

TEST_F(DefaultsLoaderTest, EmptyKeySkippedParsingContinues) {
  // "=value" has an empty key before '='; ValidateKey rejects it.
  const auto f = WritePropFile(
      "=value\n"
      "net.hostname=box\n");
  EXPECT_EQ(1, LoadDefaultsFile(f.c_str(), *store_));
  EXPECT_EQ("box", Get("net.hostname"));
}

TEST_F(DefaultsLoaderTest, InvalidKeySkippedParsingContinues) {
  const auto f = WritePropFile(
      "bad..key=value\n"
      "net.hostname=box\n");
  // "bad..key" fails ValidateKey; only "net.hostname" succeeds.
  EXPECT_EQ(1, LoadDefaultsFile(f.c_str(), *store_));
  EXPECT_EQ("box", Get("net.hostname"));
}

TEST_F(DefaultsLoaderTest, ValueTooLongSkipped) {
  const std::string too_long(SYSPROP_MAX_VALUE_LENGTH, 'x');
  const auto f = WritePropFile(("net.bad=" + too_long + "\nnet.hostname=box\n").c_str());
  EXPECT_EQ(1, LoadDefaultsFile(f.c_str(), *store_));
  EXPECT_EQ("box", Get("net.hostname"));
  EXPECT_EQ("", Get("net.bad"));  // must not have been set
}

// A line longer than fgets buffer (SYSPROP_MAX_KEY_LENGTH + SYSPROP_MAX_VALUE_LENGTH + 2 = 514)
// is split across two fgets() calls. The truncated first chunk likely fails to set
// (value is either too long or the key is malformed), and the continuation chunk
// (no '=') is also skipped. Parsing must continue and load the next valid line.
TEST_F(DefaultsLoaderTest, LineLongerThanBufferSkippedParsingContinues) {
  // Build a line whose key=value content is > 514 bytes so fgets splits it.
  const std::string long_value(600, 'v');  // "net.long=" + 600 'v's = 609 bytes
  const auto f = WritePropFile(("net.long=" + long_value + "\nnet.hostname=box\n").c_str());
  // net.long fails (value too long after split or just too long outright).
  // net.hostname is a separate line and must be loaded successfully.
  const int n = LoadDefaultsFile(f.c_str(), *store_);
  EXPECT_GE(n, 1);  // at least net.hostname loaded
  EXPECT_EQ("box", Get("net.hostname"));
}

// ── ro.* semantics ────────────────────────────────────────────────────────────

TEST_F(DefaultsLoaderTest, RoPropertyBecomesImmutableAfterLoad) {
  const auto f = WritePropFile("ro.build.version=1.0\n");
  ASSERT_EQ(1, LoadDefaultsFile(f.c_str(), *store_));
  EXPECT_EQ(SYSPROP_ERR_READ_ONLY, store_->Set("ro.build.version", "evil"));
  EXPECT_EQ("1.0", Get("ro.build.version"));
}

TEST_F(DefaultsLoaderTest, DuplicateRoPropertySecondDefinitionRejected) {
  // Two ro.* lines with the same key: only the first one wins.
  const auto f = WritePropFile(
      "ro.hw.sku=original\n"
      "ro.hw.sku=hacked\n");
  // First load succeeds, second fails (READ_ONLY). Count is 1, not 2.
  EXPECT_EQ(1, LoadDefaultsFile(f.c_str(), *store_));
  EXPECT_EQ("original", Get("ro.hw.sku"));
}

TEST_F(DefaultsLoaderTest, RoPropertyAlreadySetInStoreIsRejected) {
  // If a ro.* property already exists in the store before loading, the file's
  // definition is rejected and the existing value is preserved.
  ASSERT_EQ(SYSPROP_OK, store_->SetInit("ro.hw.sku", "factory"));
  const auto f = WritePropFile("ro.hw.sku=override\n");
  EXPECT_EQ(0, LoadDefaultsFile(f.c_str(), *store_));
  EXPECT_EQ("factory", Get("ro.hw.sku"));
}

// ── persist.* semantics ───────────────────────────────────────────────────────

TEST_F(DefaultsLoaderTest, PersistPropertyWrittenToPersistentOnly) {
  const auto f = WritePropFile("persist.wifi.ssid=HomeNet\n");
  ASSERT_EQ(1, LoadDefaultsFile(f.c_str(), *store_));
  EXPECT_FALSE(FileExists(rt_dir_, "persist.wifi.ssid"));
  EXPECT_TRUE(FileExists(ps_dir_, "persist.wifi.ssid"));
  EXPECT_EQ("HomeNet", Get("persist.wifi.ssid"));
}

// ── Realistic build.prop ──────────────────────────────────────────────────────

TEST_F(DefaultsLoaderTest, RealisticBuildProp) {
  const auto f = WritePropFile(
      "# Factory defaults — generated at build time.\n"
      "\n"
      "ro.build.version=2.3.1\n"
      "ro.build.date=2025-06-01\n"
      "ro.hw.board=armv8-a\n"
      "ro.hw.sku=standard\n"
      "\n"
      "# Mutable runtime defaults.\n"
      "net.hostname=embedded-device\n"
      "sys.log.level=info\n"
      "\n"
      "persist.tz=UTC\n");

  EXPECT_EQ(7, LoadDefaultsFile(f.c_str(), *store_));

  EXPECT_EQ("2.3.1", Get("ro.build.version"));
  EXPECT_EQ("2025-06-01", Get("ro.build.date"));
  EXPECT_EQ("armv8-a", Get("ro.hw.board"));
  EXPECT_EQ("standard", Get("ro.hw.sku"));
  EXPECT_EQ("embedded-device", Get("net.hostname"));
  EXPECT_EQ("info", Get("sys.log.level"));
  EXPECT_EQ("UTC", Get("persist.tz"));

  // All ro.* must now be immutable.
  EXPECT_EQ(SYSPROP_ERR_READ_ONLY, store_->Set("ro.build.version", "evil"));
  EXPECT_EQ(SYSPROP_ERR_READ_ONLY, store_->Set("ro.hw.sku", "evil"));
}
