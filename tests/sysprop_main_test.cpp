// Tests for cli_commands.h: DoList, CmdGetprop, CmdSetprop, CmdDelete.
//
// Each test constructs a real FileBackend + PropertyStore in a temp dir so the
// full policy layer (ro.*, persist.*) is exercised together with the CLI.
// stdout is captured by redirecting STDOUT_FILENO to a pipe.

#include "cli_commands.h"

#include <fcntl.h>
#include <unistd.h>

#include <sysprop/sysprop.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <string>

#include <gtest/gtest.h>

#include "file_backend.h"
#include "property_store.h"

using sysprop::internal::FileBackend;
using sysprop::internal::PropertyStore;

namespace {

// ── Temp-dir fixture ──────────────────────────────────────────────────────────

class CliTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const std::string base = std::string(testing::TempDir()) + "sysprop_cli_XXXXXX";
    std::vector<char> tmpl(base.begin(), base.end());
    tmpl.push_back('\0');
    const char* d = mkdtemp(tmpl.data());
    ASSERT_NE(d, nullptr) << strerror(errno);
    dir_ = d;

    runtime_ = std::make_unique<FileBackend>(dir_.c_str());
    store_   = std::make_unique<PropertyStore>(runtime_.get(), nullptr);
  }

  void TearDown() override {
    // Restore stdout in case a test left the redirect open
    if (saved_stdout_ >= 0) {
      dup2(saved_stdout_, STDOUT_FILENO);
      close(saved_stdout_);
      saved_stdout_ = -1;
    }
  }

  // Set a property directly via the store (bypassing CLI).
  void Set(const char* key, const char* value) {
    ASSERT_EQ(store_->Set(key, value), SYSPROP_OK);
  }

  // ── stdout capture helpers ────────────────────────────────────────────────

  // Redirect STDOUT_FILENO to a pipe. Call ReadCapture() after the function
  // under test to get the captured output.
  void BeginCapture() {
    ASSERT_EQ(pipe(pipe_fds_), 0);
    saved_stdout_ = dup(STDOUT_FILENO);
    ASSERT_GE(saved_stdout_, 0);
    ASSERT_EQ(dup2(pipe_fds_[1], STDOUT_FILENO), STDOUT_FILENO);
    close(pipe_fds_[1]);
    pipe_fds_[1] = -1;
  }

  std::string EndCapture() {
    // Flush libc's buffer before restoring the fd.
    fflush(stdout);
    dup2(saved_stdout_, STDOUT_FILENO);
    close(saved_stdout_);
    saved_stdout_ = -1;

    std::string out;
    char buf[256];
    ssize_t n = 0;
    while ((n = read(pipe_fds_[0], buf, sizeof(buf))) > 0) {
      out.append(buf, static_cast<std::size_t>(n));
    }
    close(pipe_fds_[0]);
    pipe_fds_[0] = -1;
    return out;
  }

  std::string dir_;
  std::unique_ptr<FileBackend>   runtime_;
  std::unique_ptr<PropertyStore> store_;

 private:
  int pipe_fds_[2]  = {-1, -1};
  int saved_stdout_ = -1;
};

// ── Argv helpers ─────────────────────────────────────────────────────────────

// Build a mutable argv array from a list of string literals so it can be
// passed to the CLI functions (which take char*[], not const char*[]).
template<std::size_t N>
std::array<char*, N> MakeArgv(const char* (&src)[N]) {
  std::array<char*, N> out;
  for (std::size_t i = 0; i < N; ++i) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    out[i] = const_cast<char*>(src[i]);
  }
  return out;
}

}  // namespace

// ── DoList ────────────────────────────────────────────────────────────────────

TEST_F(CliTest, DoListEmptyStoreProducesNoOutput) {
  BeginCapture();
  const int rc = sysprop::tools::DoList(*store_);
  const std::string out = EndCapture();
  EXPECT_EQ(rc, 0);
  EXPECT_TRUE(out.empty());
}

TEST_F(CliTest, DoListOnePropertyPrintsBracketed) {
  Set("hw.version", "2");
  BeginCapture();
  const int rc = sysprop::tools::DoList(*store_);
  const std::string out = EndCapture();
  EXPECT_EQ(rc, 0);
  EXPECT_EQ(out, "[hw.version]: [2]\n");
}

TEST_F(CliTest, DoListMultiplePropertiesSortedAlphabetically) {
  Set("z.last", "Z");
  Set("a.first", "A");
  Set("m.mid", "M");
  BeginCapture();
  const int rc = sysprop::tools::DoList(*store_);
  const std::string out = EndCapture();
  EXPECT_EQ(rc, 0);
  // Verify order: a.first < m.mid < z.last
  const std::size_t pa = out.find("[a.first]");
  const std::size_t pm = out.find("[m.mid]");
  const std::size_t pz = out.find("[z.last]");
  ASSERT_NE(pa, std::string::npos);
  ASSERT_NE(pm, std::string::npos);
  ASSERT_NE(pz, std::string::npos);
  EXPECT_LT(pa, pm);
  EXPECT_LT(pm, pz);
}

TEST_F(CliTest, DoListOutputFormatIsKeyColon) {
  Set("foo.bar", "hello world");
  BeginCapture();
  (void)sysprop::tools::DoList(*store_);
  const std::string out = EndCapture();
  EXPECT_EQ(out, "[foo.bar]: [hello world]\n");
}

// ── CmdGetprop ────────────────────────────────────────────────────────────────

// getprop with no key → list all
TEST_F(CliTest, CmdGetpropNoArgListsAll) {
  Set("list.key", "listed");
  const char* argv[] = {"getprop"};
  auto av = MakeArgv(argv);
  BeginCapture();
  const int rc = sysprop::tools::CmdGetprop(1, av.data(), *store_);
  const std::string out = EndCapture();
  EXPECT_EQ(rc, 0);
  EXPECT_NE(out.find("[list.key]: [listed]"), std::string::npos);
}

// getprop key → found, prints value
TEST_F(CliTest, CmdGetpropFoundPrintsValue) {
  Set("net.hostname", "mybox");
  const char* argv[] = {"getprop", "net.hostname"};
  auto av = MakeArgv(argv);
  BeginCapture();
  const int rc = sysprop::tools::CmdGetprop(2, av.data(), *store_);
  const std::string out = EndCapture();
  EXPECT_EQ(rc, 0);
  EXPECT_EQ(out, "mybox\n");
}

// getprop key → not found, no default → prints empty line, returns 0
TEST_F(CliTest, CmdGetpropNotFoundNoDefaultPrintsEmptyLine) {
  const char* argv[] = {"getprop", "no.such.key"};
  auto av = MakeArgv(argv);
  BeginCapture();
  const int rc = sysprop::tools::CmdGetprop(2, av.data(), *store_);
  const std::string out = EndCapture();
  EXPECT_EQ(rc, 0);
  EXPECT_EQ(out, "\n");
}

// getprop key default → not found, prints default, returns 0
TEST_F(CliTest, CmdGetpropNotFoundWithDefaultPrintsDefault) {
  const char* argv[] = {"getprop", "no.such.key", "fallback"};
  auto av = MakeArgv(argv);
  BeginCapture();
  const int rc = sysprop::tools::CmdGetprop(3, av.data(), *store_);
  const std::string out = EndCapture();
  EXPECT_EQ(rc, 0);
  EXPECT_EQ(out, "fallback\n");
}

// getprop key default → found, prints actual value (not default)
TEST_F(CliTest, CmdGetpropFoundIgnoresDefault) {
  Set("hw.id", "realval");
  const char* argv[] = {"getprop", "hw.id", "fallback"};
  auto av = MakeArgv(argv);
  BeginCapture();
  const int rc = sysprop::tools::CmdGetprop(3, av.data(), *store_);
  const std::string out = EndCapture();
  EXPECT_EQ(rc, 0);
  EXPECT_EQ(out, "realval\n");
}

// ── CmdSetprop ────────────────────────────────────────────────────────────────

// setprop with too few args → usage error, returns 1
TEST_F(CliTest, CmdSetpropTooFewArgsReturnsOne) {
  const char* argv[] = {"setprop", "only.key"};
  auto av = MakeArgv(argv);
  EXPECT_EQ(sysprop::tools::CmdSetprop(2, av.data(), *store_), 1);
}

// setprop no args at all → usage error, returns 1
TEST_F(CliTest, CmdSetpropNoArgsReturnsOne) {
  const char* argv[] = {"setprop"};
  auto av = MakeArgv(argv);
  EXPECT_EQ(sysprop::tools::CmdSetprop(1, av.data(), *store_), 1);
}

// setprop key value → succeeds, value readable afterwards
TEST_F(CliTest, CmdSetpropSuccessStoresValue) {
  const char* argv[] = {"setprop", "debug.level", "verbose"};
  auto av = MakeArgv(argv);
  EXPECT_EQ(sysprop::tools::CmdSetprop(3, av.data(), *store_), 0);

  char buf[SYSPROP_MAX_VALUE_LENGTH];
  EXPECT_GT(store_->Get("debug.level", buf, sizeof(buf)), 0);
  EXPECT_STREQ(buf, "verbose");
}

// setprop ro.* twice → second returns 1 (read-only)
TEST_F(CliTest, CmdSetpropRoSecondWriteReturnsOne) {
  const char* set1[] = {"setprop", "ro.hw.sku", "A"};
  auto av1 = MakeArgv(set1);
  EXPECT_EQ(sysprop::tools::CmdSetprop(3, av1.data(), *store_), 0);

  const char* set2[] = {"setprop", "ro.hw.sku", "B"};
  auto av2 = MakeArgv(set2);
  EXPECT_EQ(sysprop::tools::CmdSetprop(3, av2.data(), *store_), 1);
}

// setprop invalid key → returns 1
TEST_F(CliTest, CmdSetpropInvalidKeyReturnsOne) {
  const char* argv[] = {"setprop", "bad key!", "val"};
  auto av = MakeArgv(argv);
  EXPECT_EQ(sysprop::tools::CmdSetprop(3, av.data(), *store_), 1);
}

// ── CmdDelete ─────────────────────────────────────────────────────────────────

// delete with no key → usage error, returns 1
TEST_F(CliTest, CmdDeleteNoKeyReturnsOne) {
  const char* argv[] = {"sysprop"};
  auto av = MakeArgv(argv);
  EXPECT_EQ(sysprop::tools::CmdDelete(1, av.data(), *store_), 1);
}

// delete existing key → succeeds, key gone
TEST_F(CliTest, CmdDeleteExistingKeySucceeds) {
  Set("tmp.data", "42");
  const char* argv[] = {"sysprop", "tmp.data"};
  auto av = MakeArgv(argv);
  EXPECT_EQ(sysprop::tools::CmdDelete(2, av.data(), *store_), 0);

  char buf[SYSPROP_MAX_VALUE_LENGTH];
  EXPECT_EQ(store_->Get("tmp.data", buf, sizeof(buf)), SYSPROP_ERR_NOT_FOUND);
}

// delete nonexistent key → returns non-zero
TEST_F(CliTest, CmdDeleteNonexistentKeyReturnsNonzero) {
  const char* argv[] = {"sysprop", "no.such.key"};
  auto av = MakeArgv(argv);
  EXPECT_NE(sysprop::tools::CmdDelete(2, av.data(), *store_), 0);
}

// delete ro.* key → denied, returns 1
TEST_F(CliTest, CmdDeleteRoKeyReturnsOne) {
  Set("ro.product.model", "EVB-1");
  const char* argv[] = {"sysprop", "ro.product.model"};
  auto av = MakeArgv(argv);
  EXPECT_EQ(sysprop::tools::CmdDelete(2, av.data(), *store_), 1);

  // Verify key is still present
  char buf[SYSPROP_MAX_VALUE_LENGTH];
  EXPECT_GT(store_->Get("ro.product.model", buf, sizeof(buf)), 0);
  EXPECT_STREQ(buf, "EVB-1");
}

// ── DoList with empty-value properties ───────────────────────────────────────

TEST_F(CliTest, DoListEmptyValueProperty) {
  Set("empty.val", "");
  BeginCapture();
  const int rc = sysprop::tools::DoList(*store_);
  const std::string out = EndCapture();
  EXPECT_EQ(rc, 0);
  EXPECT_EQ(out, "[empty.val]: []\n");
}
