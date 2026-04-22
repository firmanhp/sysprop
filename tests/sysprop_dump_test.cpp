// Tests for sysprop_dump() and CmdList.
//
// DumpTest:   exercises sysprop_dump() via swap_store() injection — real
//             FileBackend + FilePropertyStore in temp dirs.
// CmdListTest: exercises CmdList() directly with stdout capture, following the
//             same fixture pattern as sysprop_main_test.cpp.

#include <fcntl.h>
#include <unistd.h>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <sysprop/sysprop.h>
#include <sysprop/testing/internal.h>
#include <sysprop/testing/mock_property_store.h>

#include "cli_commands.h"
#include "file_backend.h"
#include "file_property_store.h"

using sysprop::internal::FileBackend;
using sysprop::internal::FilePropertyStore;
using sysprop::testing::swap_store;

namespace {

// ── Standalone tests using MockPropertyStore ──────────────────────────────────

TEST(DumpMockTest, EmptyStoreReturnsEmptyString) {
  sysprop::testing::MockPropertyStore mock;
  EXPECT_EQ(sysprop_dump(), "");
}

TEST(DumpMockTest, SingleEntryFormattedCorrectly) {
  sysprop::testing::MockPropertyStore mock;
  mock.Set("mock.key", "mock.val");
  EXPECT_EQ(sysprop_dump(), "mock.key=mock.val\n");
}

// ── DumpTest ──────────────────────────────────────────────────────────────────
//
// Uses SetUpTestSuite so a single FilePropertyStore is shared across tests,
// each test uses unique key prefixes to avoid interference.

class DumpTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    std::string rt = std::string(testing::TempDir()) + "sysprop_dump_rt_XXXXXX";
    std::string ps = std::string(testing::TempDir()) + "sysprop_dump_ps_XXXXXX";
    ASSERT_NE(::mkdtemp(rt.data()), nullptr);
    ASSERT_NE(::mkdtemp(ps.data()), nullptr);
    rt_dir_ = rt;
    ps_dir_ = ps;
    rt_backend_  = std::make_unique<FileBackend>(rt_dir_.c_str());
    ps_backend_  = std::make_unique<FileBackend>(ps_dir_.c_str());
    store_       = std::make_unique<FilePropertyStore>(rt_backend_.get(), ps_backend_.get());
    prev_store_  = swap_store(store_.get());
  }

  static void TearDownTestSuite() {
    swap_store(prev_store_);
    store_.reset();
    ps_backend_.reset();
    rt_backend_.reset();
  }

  static std::string rt_dir_;
  static std::string ps_dir_;
  static std::unique_ptr<FileBackend> rt_backend_;
  static std::unique_ptr<FileBackend> ps_backend_;
  static std::unique_ptr<FilePropertyStore> store_;
  static sysprop::internal::PropertyStore* prev_store_;
};

std::string DumpTest::rt_dir_;
std::string DumpTest::ps_dir_;
std::unique_ptr<FileBackend> DumpTest::rt_backend_;
std::unique_ptr<FileBackend> DumpTest::ps_backend_;
std::unique_ptr<FilePropertyStore> DumpTest::store_;
sysprop::internal::PropertyStore* DumpTest::prev_store_ = nullptr;

TEST_F(DumpTest, VolatileKeyAppearsInDump) {
  ASSERT_EQ(SYSPROP_OK, sysprop_set("dump.volatile.a", "hello"));
  const std::string out = sysprop_dump();
  EXPECT_NE(out.find("dump.volatile.a=hello\n"), std::string::npos);
}

TEST_F(DumpTest, ReadOnlyKeyAppearsInDump) {
  ASSERT_EQ(SYSPROP_OK, store_->SetInit("ro.dump.version", "1.0"));
  const std::string out = sysprop_dump();
  EXPECT_NE(out.find("ro.dump.version=1.0\n"), std::string::npos);
}

TEST_F(DumpTest, PersistKeyAppearsInDump) {
  ASSERT_EQ(SYSPROP_OK, sysprop_set("persist.dump.ssid", "TestNet"));
  const std::string out = sysprop_dump();
  EXPECT_NE(out.find("persist.dump.ssid=TestNet\n"), std::string::npos);
}

TEST_F(DumpTest, OutputIsSorted) {
  ASSERT_EQ(SYSPROP_OK, sysprop_set("dump.sort.z", "last"));
  ASSERT_EQ(SYSPROP_OK, sysprop_set("dump.sort.a", "first"));
  const std::string out = sysprop_dump();
  const auto pos_a = out.find("dump.sort.a=first\n");
  const auto pos_z = out.find("dump.sort.z=last\n");
  ASSERT_NE(pos_a, std::string::npos);
  ASSERT_NE(pos_z, std::string::npos);
  EXPECT_LT(pos_a, pos_z);
}

TEST_F(DumpTest, FormatIsKeyEqualsValue) {
  ASSERT_EQ(SYSPROP_OK, sysprop_set("dump.fmt.key", "myvalue"));
  const std::string out = sysprop_dump();
  EXPECT_NE(out.find("dump.fmt.key=myvalue\n"), std::string::npos);
}

// ── CmdListTest ───────────────────────────────────────────────────────────────
//
// Tests CmdList() directly with stdout capture. Uses a fresh store per test.

class CmdListTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const std::string base = std::string(testing::TempDir()) + "sysprop_list_XXXXXX";
    std::vector<char> tmpl(base.begin(), base.end());
    tmpl.push_back('\0');
    const char* d = ::mkdtemp(tmpl.data());
    ASSERT_NE(d, nullptr) << strerror(errno);
    dir_ = d;
    runtime_ = std::make_unique<FileBackend>(dir_.c_str());
    store_   = std::make_unique<FilePropertyStore>(runtime_.get(), nullptr);
  }

  void TearDown() override {
    if (saved_stdout_ >= 0) {
      dup2(saved_stdout_, STDOUT_FILENO);
      close(saved_stdout_);
      saved_stdout_ = -1;
    }
  }

  void Set(const char* key, const char* value) {
    ASSERT_EQ(store_->Set(key, value), SYSPROP_OK);
  }

  void BeginCapture() {
    ASSERT_EQ(pipe(pipe_fds_), 0);
    saved_stdout_ = dup(STDOUT_FILENO);
    ASSERT_GE(saved_stdout_, 0);
    ASSERT_EQ(dup2(pipe_fds_[1], STDOUT_FILENO), STDOUT_FILENO);
    close(pipe_fds_[1]);
    pipe_fds_[1] = -1;
  }

  std::string EndCapture() {
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

TEST_F(CmdListTest, EmptyStoreProducesNoOutput) {
  BeginCapture();
  const int rc = sysprop::tools::CmdList(*store_);
  const std::string out = EndCapture();
  EXPECT_EQ(rc, 0);
  EXPECT_EQ(out, "");
}

TEST_F(CmdListTest, SinglePropertyPrinted) {
  Set("list.key", "val");
  BeginCapture();
  const int rc = sysprop::tools::CmdList(*store_);
  const std::string out = EndCapture();
  EXPECT_EQ(rc, 0);
  EXPECT_EQ(out, "list.key=val\n");
}

TEST_F(CmdListTest, MultiplePropertiesSorted) {
  Set("list.z", "last");
  Set("list.a", "first");
  Set("list.m", "middle");
  BeginCapture();
  const int rc = sysprop::tools::CmdList(*store_);
  const std::string out = EndCapture();
  EXPECT_EQ(rc, 0);
  EXPECT_EQ(out, "list.a=first\nlist.m=middle\nlist.z=last\n");
}

TEST_F(CmdListTest, ReadOnlyKeyIncluded) {
  ASSERT_EQ(SYSPROP_OK, store_->SetInit("ro.list.ver", "2.0"));
  BeginCapture();
  sysprop::tools::CmdList(*store_);
  const std::string out = EndCapture();
  EXPECT_NE(out.find("ro.list.ver=2.0\n"), std::string::npos);
}

}  // namespace
