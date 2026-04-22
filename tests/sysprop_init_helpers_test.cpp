// Tests for init_helpers.h: MkdirP, CleanupTmpFiles, and ParseInitArgs.

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <sys/stat.h>

#include "init_helpers.h"

namespace {

// ── Helpers ───────────────────────────────────────────────────────────────────

bool PathExists(const std::string& path) {
  struct stat st {};
  return ::stat(path.c_str(), &st) == 0;
}

bool IsDirectory(const std::string& path) {
  struct stat st {};
  return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// Create a regular file at path with the given content.
bool WriteFile(const std::string& path, const char* content = "") {
  const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) {
    return false;
  }
  const ssize_t n = ::write(fd, content, std::strlen(content));
  ::close(fd);
  return n >= 0;
}

// ── MkdirP fixture ────────────────────────────────────────────────────────────

class MkdirPTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create a temp root that we own; MkdirP tests create children inside it.
    const std::string tmpl = std::string(testing::TempDir()) + "sysprop_mkdirp_XXXXXX";
    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    const char* d = mkdtemp(buf.data());
    ASSERT_NE(d, nullptr) << strerror(errno);
    root_ = d;
  }

  std::string root_;
};

}  // namespace

// MkdirP on an already-existing directory returns true.
TEST_F(MkdirPTest, ExistingDirectoryReturnsTrue) {
  EXPECT_TRUE(sysprop::tools::MkdirP(root_.c_str()));
}

// MkdirP creates a single new child directory.
TEST_F(MkdirPTest, CreatesSingleDirectory) {
  const std::string target = root_ + "/newdir";
  EXPECT_TRUE(sysprop::tools::MkdirP(target.c_str()));
  EXPECT_TRUE(IsDirectory(target));
}

// MkdirP creates nested directories (mkdir -p semantics).
TEST_F(MkdirPTest, CreatesNestedDirectories) {
  const std::string target = root_ + "/a/b/c/d";
  EXPECT_TRUE(sysprop::tools::MkdirP(target.c_str()));
  EXPECT_TRUE(IsDirectory(root_ + "/a"));
  EXPECT_TRUE(IsDirectory(root_ + "/a/b"));
  EXPECT_TRUE(IsDirectory(root_ + "/a/b/c"));
  EXPECT_TRUE(IsDirectory(target));
}

// MkdirP is idempotent — calling twice on the same path succeeds.
TEST_F(MkdirPTest, IdempotentOnExistingPath) {
  const std::string target = root_ + "/idempotent";
  EXPECT_TRUE(sysprop::tools::MkdirP(target.c_str()));
  EXPECT_TRUE(sysprop::tools::MkdirP(target.c_str()));
  EXPECT_TRUE(IsDirectory(target));
}

// MkdirP on a path whose parent is a regular file (not a dir) returns false.
TEST_F(MkdirPTest, FailsWhenParentIsFile) {
  const std::string file = root_ + "/notadir";
  ASSERT_TRUE(WriteFile(file));
  const std::string target = file + "/child";
  EXPECT_FALSE(sysprop::tools::MkdirP(target.c_str()));
}

// MkdirP without write permission on an ancestor returns false (skip if root).
TEST_F(MkdirPTest, FailsWithoutWritePermission) {
  if (::getuid() == 0) {
    GTEST_SKIP() << "Running as root — permission checks not effective";
  }
  const std::string locked = root_ + "/locked";
  ASSERT_EQ(::mkdir(locked.c_str(), 0555), 0);
  const std::string target = locked + "/child";
  EXPECT_FALSE(sysprop::tools::MkdirP(target.c_str()));
}

// ── CleanupTmpFiles fixture ───────────────────────────────────────────────────

namespace {
class CleanupTmpFilesTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const std::string tmpl = std::string(testing::TempDir()) + "sysprop_cleanup_XXXXXX";
    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    const char* d = mkdtemp(buf.data());
    ASSERT_NE(d, nullptr) << strerror(errno);
    dir_ = d;
  }

  // Create a file named `name` in the test directory.
  void Touch(const std::string& name) { ASSERT_TRUE(WriteFile(dir_ + "/" + name)); }

  bool Exists(const std::string& name) { return PathExists(dir_ + "/" + name); }

  std::string dir_;
};
}  // namespace

// CleanupTmpFiles removes files whose names start with ".tmp.".
TEST_F(CleanupTmpFilesTest, RemovesTmpPrefixedFiles) {
  Touch(".tmp.ro.product.model.1234");
  Touch(".tmp.persist.wifi.ssid.5678");
  sysprop::tools::CleanupTmpFiles(dir_.c_str());
  EXPECT_FALSE(Exists(".tmp.ro.product.model.1234"));
  EXPECT_FALSE(Exists(".tmp.persist.wifi.ssid.5678"));
}

// CleanupTmpFiles leaves regular (non-.tmp.) files untouched.
TEST_F(CleanupTmpFilesTest, LeavesRegularFilesAlone) {
  Touch("ro.build.version");
  Touch("net.hostname");
  sysprop::tools::CleanupTmpFiles(dir_.c_str());
  EXPECT_TRUE(Exists("ro.build.version"));
  EXPECT_TRUE(Exists("net.hostname"));
}

// CleanupTmpFiles removes tmp files but leaves regular files.
TEST_F(CleanupTmpFilesTest, MixedDirOnlyRemovesTmp) {
  Touch("persist.wifi.ssid");
  Touch(".tmp.persist.wifi.ssid.999");
  sysprop::tools::CleanupTmpFiles(dir_.c_str());
  EXPECT_TRUE(Exists("persist.wifi.ssid"));
  EXPECT_FALSE(Exists(".tmp.persist.wifi.ssid.999"));
}

// CleanupTmpFiles is a no-op on an empty directory.
TEST_F(CleanupTmpFilesTest, EmptyDirectoryNoOp) {
  EXPECT_NO_FATAL_FAILURE(sysprop::tools::CleanupTmpFiles(dir_.c_str()));
}

// CleanupTmpFiles is a no-op if the directory does not exist (no crash).
TEST_F(CleanupTmpFilesTest, NonexistentDirectoryNoOp) {
  const std::string gone = dir_ + "/does_not_exist";
  EXPECT_NO_FATAL_FAILURE(sysprop::tools::CleanupTmpFiles(gone.c_str()));
}

// CleanupTmpFiles leaves dot-only names untouched ("." and "..").
TEST_F(CleanupTmpFilesTest, SkipsDotAndDotDot) {
  // These are virtual entries; we just verify no crash and no entries removed
  // from an otherwise-empty directory.
  EXPECT_NO_FATAL_FAILURE(sysprop::tools::CleanupTmpFiles(dir_.c_str()));
}

// A file literally named ".tmp." (five chars) is treated as a tmp file.
TEST_F(CleanupTmpFilesTest, FileLiterallyDotTmpDotIsRemoved) {
  Touch(".tmp.");
  sysprop::tools::CleanupTmpFiles(dir_.c_str());
  EXPECT_FALSE(Exists(".tmp."));
}

// Multiple crash-era tmp files are all cleaned in one pass.
TEST_F(CleanupTmpFilesTest, MultipleOldCrashFilesAllRemoved) {
  for (int i = 0; i < 10; ++i) {
    Touch(".tmp.key." + std::to_string(i));
  }
  sysprop::tools::CleanupTmpFiles(dir_.c_str());
  for (int i = 0; i < 10; ++i) {
    EXPECT_FALSE(Exists(".tmp.key." + std::to_string(i)));
  }
}

// ── ParseInitArgs ─────────────────────────────────────────────────────────────

namespace {

// Owns mutable copies of string literals so they can be passed to functions
// that take char*[] (matching POSIX main() convention).
struct MutableArgv {
  MutableArgv(std::initializer_list<const char*> args) {
    strings_.assign(args.begin(), args.end());
    for (auto& s : strings_) ptrs_.push_back(s.data());
  }
  int argc() const { return static_cast<int>(ptrs_.size()); }
  char** data() { return ptrs_.data(); }

 private:
  std::vector<std::string> strings_;
  std::vector<char*> ptrs_;
};

}  // namespace

// No args → defaults_file is null.
TEST(ParseInitArgsTest, DefaultsWhenNoArgs) {
  MutableArgv av{"sysprop-init"};
  const auto args = sysprop::tools::ParseInitArgs(av.argc(), av.data());
  EXPECT_EQ(args.defaults_file, nullptr);
}

// Positional (non-flag) argument is treated as defaults_file.
TEST(ParseInitArgsTest, PositionalArgIsDefaultsFile) {
  MutableArgv av{"sysprop-init", "/etc/build.prop"};
  const auto args = sysprop::tools::ParseInitArgs(av.argc(), av.data());
  EXPECT_STREQ(args.defaults_file, "/etc/build.prop");
}

// Unknown --flag is silently logged but does not abort.
TEST(ParseInitArgsTest, UnknownFlagIgnored) {
  MutableArgv av{"sysprop-init", "--unknown-flag"};
  EXPECT_NO_FATAL_FAILURE((void)sysprop::tools::ParseInitArgs(av.argc(), av.data()));
}
