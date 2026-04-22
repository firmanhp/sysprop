// Tests for cli_commands.h: DoList, CmdGetprop, CmdSetprop, CmdDelete.
//
// Each test constructs a real FileBackend + FilePropertyStore in a temp dir so the
// full policy layer (ro.*, persist.*) is exercised together with the CLI.
// stdout is captured by redirecting STDOUT_FILENO to a pipe.

#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <sysprop/sysprop.h>

#include "cli_commands.h"
#include "file_backend.h"
#include "file_property_store.h"

using sysprop::internal::FileBackend;
using sysprop::internal::FilePropertyStore;

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
    store_ = std::make_unique<FilePropertyStore>(*runtime_, *runtime_);
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
  void Set(const char* key, const char* value) { ASSERT_EQ(store_->Set(key, value), SYSPROP_OK); }

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
  std::unique_ptr<FileBackend> runtime_;
  std::unique_ptr<FilePropertyStore> store_;

 private:
  int pipe_fds_[2] = {-1, -1};
  int saved_stdout_ = -1;
};

// ── Argv helpers ─────────────────────────────────────────────────────────────

// Owns mutable copies of string literals so they can be passed to CLI
// functions that take char*[] (matching POSIX main() convention).
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

// ── CmdGetprop ────────────────────────────────────────────────────────────────

// getprop with no key → usage error
TEST_F(CliTest, CmdGetpropNoArgReturnsOne) {
  MutableArgv av{"getprop"};
  EXPECT_EQ(sysprop::tools::CmdGetprop(av.argc(), av.data(), *store_), 1);
}

// getprop key → found, prints value
TEST_F(CliTest, CmdGetpropFoundPrintsValue) {
  Set("net.hostname", "mybox");
  MutableArgv av{"getprop", "net.hostname"};
  BeginCapture();
  const int rc = sysprop::tools::CmdGetprop(av.argc(), av.data(), *store_);
  const std::string out = EndCapture();
  EXPECT_EQ(rc, 0);
  EXPECT_EQ(out, "mybox\n");
}

// getprop key → not found, no default → prints empty line, returns 0
TEST_F(CliTest, CmdGetpropNotFoundNoDefaultPrintsEmptyLine) {
  MutableArgv av{"getprop", "no.such.key"};
  BeginCapture();
  const int rc = sysprop::tools::CmdGetprop(av.argc(), av.data(), *store_);
  const std::string out = EndCapture();
  EXPECT_EQ(rc, 0);
  EXPECT_EQ(out, "\n");
}

// getprop key default → not found, prints default, returns 0
TEST_F(CliTest, CmdGetpropNotFoundWithDefaultPrintsDefault) {
  MutableArgv av{"getprop", "no.such.key", "fallback"};
  BeginCapture();
  const int rc = sysprop::tools::CmdGetprop(av.argc(), av.data(), *store_);
  const std::string out = EndCapture();
  EXPECT_EQ(rc, 0);
  EXPECT_EQ(out, "fallback\n");
}

// getprop key default → found, prints actual value (not default)
TEST_F(CliTest, CmdGetpropFoundIgnoresDefault) {
  Set("hw.id", "realval");
  MutableArgv av{"getprop", "hw.id", "fallback"};
  BeginCapture();
  const int rc = sysprop::tools::CmdGetprop(av.argc(), av.data(), *store_);
  const std::string out = EndCapture();
  EXPECT_EQ(rc, 0);
  EXPECT_EQ(out, "realval\n");
}

// ── CmdSetprop ────────────────────────────────────────────────────────────────

// setprop with too few args → usage error, returns 1
TEST_F(CliTest, CmdSetpropTooFewArgsReturnsOne) {
  MutableArgv av{"setprop", "only.key"};
  EXPECT_EQ(sysprop::tools::CmdSetprop(av.argc(), av.data(), *store_), 1);
}

// setprop no args at all → usage error, returns 1
TEST_F(CliTest, CmdSetpropNoArgsReturnsOne) {
  MutableArgv av{"setprop"};
  EXPECT_EQ(sysprop::tools::CmdSetprop(av.argc(), av.data(), *store_), 1);
}

// setprop key value → succeeds, value readable afterwards
TEST_F(CliTest, CmdSetpropSuccessStoresValue) {
  MutableArgv av{"setprop", "debug.level", "verbose"};
  EXPECT_EQ(sysprop::tools::CmdSetprop(av.argc(), av.data(), *store_), 0);

  char buf[SYSPROP_MAX_VALUE_LENGTH];
  EXPECT_GT(store_->Get("debug.level", buf, sizeof(buf)), 0);
  EXPECT_STREQ(buf, "verbose");
}

// setprop ro.* → always rejected, even the very first call
TEST_F(CliTest, CmdSetpropRoKeyAlwaysReturnsOne) {
  MutableArgv av{"setprop", "ro.hw.sku", "A"};
  EXPECT_EQ(sysprop::tools::CmdSetprop(av.argc(), av.data(), *store_), 1);
}

// setprop invalid key → returns 1
TEST_F(CliTest, CmdSetpropInvalidKeyReturnsOne) {
  MutableArgv av{"setprop", "bad key!", "val"};
  EXPECT_EQ(sysprop::tools::CmdSetprop(av.argc(), av.data(), *store_), 1);
}

// setprop key "" → empty value deletes an existing property
TEST_F(CliTest, CmdSetpropEmptyValueDeletesProperty) {
  Set("tmp.item", "old");
  MutableArgv av{"setprop", "tmp.item", ""};
  EXPECT_EQ(sysprop::tools::CmdSetprop(av.argc(), av.data(), *store_), 0);

  char buf[SYSPROP_MAX_VALUE_LENGTH];
  EXPECT_EQ(store_->Get("tmp.item", buf, sizeof(buf)), SYSPROP_ERR_NOT_FOUND);
}

// setprop key "" → no-op when property doesn't exist (still returns 0)
TEST_F(CliTest, CmdSetpropEmptyValueOnMissingKeySucceeds) {
  MutableArgv av{"setprop", "no.such.key", ""};
  EXPECT_EQ(sysprop::tools::CmdSetprop(av.argc(), av.data(), *store_), 0);
}

// setprop key `"hello"` → surrounding double-quotes are stripped; stores hello
TEST_F(CliTest, CmdSetpropStripsOuterDoubleQuotes) {
  MutableArgv av{"setprop", "net.host", "\"mybox\""};
  EXPECT_EQ(sysprop::tools::CmdSetprop(av.argc(), av.data(), *store_), 0);

  char buf[SYSPROP_MAX_VALUE_LENGTH];
  EXPECT_GT(store_->Get("net.host", buf, sizeof(buf)), 0);
  EXPECT_STREQ(buf, "mybox");
}

// setprop key `""` → quote-strip produces empty → deletes property
TEST_F(CliTest, CmdSetpropEmptyQuotedValueDeletesProperty) {
  Set("tmp.item", "old");
  MutableArgv av{"setprop", "tmp.item", "\"\""};
  EXPECT_EQ(sysprop::tools::CmdSetprop(av.argc(), av.data(), *store_), 0);

  char buf[SYSPROP_MAX_VALUE_LENGTH];
  EXPECT_EQ(store_->Get("tmp.item", buf, sizeof(buf)), SYSPROP_ERR_NOT_FOUND);
}

// setprop ro.* "" → denied (read-only), returns 1
TEST_F(CliTest, CmdSetpropEmptyValueOnRoKeyReturnsOne) {
  ASSERT_EQ(SYSPROP_OK, store_->SetInit("ro.hw.rev", "1"));
  MutableArgv av{"setprop", "ro.hw.rev", ""};
  EXPECT_EQ(sysprop::tools::CmdSetprop(av.argc(), av.data(), *store_), 1);

  // Property must still be present.
  char buf[SYSPROP_MAX_VALUE_LENGTH];
  EXPECT_GT(store_->Get("ro.hw.rev", buf, sizeof(buf)), 0);
  EXPECT_STREQ(buf, "1");
}

// ── CmdDelete ─────────────────────────────────────────────────────────────────

// delete with no key → usage error, returns 1
TEST_F(CliTest, CmdDeleteNoKeyReturnsOne) {
  MutableArgv av{"sysprop"};
  EXPECT_EQ(sysprop::tools::CmdDelete(av.argc(), av.data(), *store_), 1);
}

// delete existing key → succeeds, key gone
TEST_F(CliTest, CmdDeleteExistingKeySucceeds) {
  Set("tmp.data", "42");
  MutableArgv av{"sysprop", "tmp.data"};
  EXPECT_EQ(sysprop::tools::CmdDelete(av.argc(), av.data(), *store_), 0);

  char buf[SYSPROP_MAX_VALUE_LENGTH];
  EXPECT_EQ(store_->Get("tmp.data", buf, sizeof(buf)), SYSPROP_ERR_NOT_FOUND);
}

// delete nonexistent key → returns non-zero
TEST_F(CliTest, CmdDeleteNonexistentKeyReturnsNonzero) {
  MutableArgv av{"sysprop", "no.such.key"};
  EXPECT_NE(sysprop::tools::CmdDelete(av.argc(), av.data(), *store_), 0);
}

// delete ro.* key → denied, returns 1
TEST_F(CliTest, CmdDeleteRoKeyReturnsOne) {
  ASSERT_EQ(SYSPROP_OK, store_->SetInit("ro.product.model", "EVB-1"));
  MutableArgv av{"sysprop", "ro.product.model"};
  EXPECT_EQ(sysprop::tools::CmdDelete(av.argc(), av.data(), *store_), 1);

  // Verify key is still present
  char buf[SYSPROP_MAX_VALUE_LENGTH];
  EXPECT_GT(store_->Get("ro.product.model", buf, sizeof(buf)), 0);
  EXPECT_STREQ(buf, "EVB-1");
}

