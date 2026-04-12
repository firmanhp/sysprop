#include "file_backend.h"

#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <sys/stat.h>
#include <sysprop/sysprop.h>

using sysprop::internal::FileBackend;
using sysprop::internal::MakeVisitor;

// ── Fixture ───────────────────────────────────────────────────────────────────

class FileBackendTest : public ::testing::Test {
 protected:
  void SetUp() override {
    std::string tmpl = testing::TempDir() + "sysprop_test_XXXXXX";
    ASSERT_NE(::mkdtemp(tmpl.data()), nullptr) << "mkdtemp failed: " << strerror(errno);
    tmp_dir_ = tmpl;
    backend_ = std::make_unique<FileBackend>(tmp_dir_.c_str());
  }

  std::string tmp_dir_;
  std::unique_ptr<FileBackend> backend_;
  char buf_[SYSPROP_MAX_VALUE_LENGTH] = {};
};

// ── Basic Get / Set / Delete ──────────────────────────────────────────────────

TEST_F(FileBackendTest, SetAndGet) {
  ASSERT_EQ(SYSPROP_OK, backend_->Set("test.key", "hello"));
  const int n = backend_->Get("test.key", buf_, sizeof(buf_));
  ASSERT_GE(n, 0);
  EXPECT_STREQ("hello", buf_);
}

TEST_F(FileBackendTest, GetNotFound) {
  EXPECT_EQ(SYSPROP_ERR_NOT_FOUND, backend_->Get("no.such.key", buf_, sizeof(buf_)));
}

TEST_F(FileBackendTest, SetOverwrite) {
  ASSERT_EQ(SYSPROP_OK, backend_->Set("test.key", "first"));
  ASSERT_EQ(SYSPROP_OK, backend_->Set("test.key", "second"));
  const int n = backend_->Get("test.key", buf_, sizeof(buf_));
  ASSERT_GE(n, 0);
  EXPECT_STREQ("second", buf_);
}

TEST_F(FileBackendTest, SetEmptyValue) {
  ASSERT_EQ(SYSPROP_OK, backend_->Set("test.empty", ""));
  const int n = backend_->Get("test.empty", buf_, sizeof(buf_));
  EXPECT_EQ(0, n);
  EXPECT_STREQ("", buf_);
}

TEST_F(FileBackendTest, Delete) {
  ASSERT_EQ(SYSPROP_OK, backend_->Set("test.del", "value"));
  ASSERT_EQ(SYSPROP_OK, backend_->Delete("test.del"));
  EXPECT_EQ(SYSPROP_ERR_NOT_FOUND, backend_->Get("test.del", buf_, sizeof(buf_)));
}

TEST_F(FileBackendTest, DeleteNotFound) {
  EXPECT_EQ(SYSPROP_ERR_NOT_FOUND, backend_->Delete("no.such.key"));
}

TEST_F(FileBackendTest, Exists) {
  EXPECT_EQ(SYSPROP_ERR_NOT_FOUND, backend_->Exists("test.key"));
  ASSERT_EQ(SYSPROP_OK, backend_->Set("test.key", "v"));
  EXPECT_EQ(SYSPROP_OK, backend_->Exists("test.key"));
}

TEST_F(FileBackendTest, GetReturnsCorrectByteCount) {
  ASSERT_EQ(SYSPROP_OK, backend_->Set("test.key", "hello"));
  const int n = backend_->Get("test.key", buf_, sizeof(buf_));
  EXPECT_EQ(5, n);  // strlen("hello") == 5
}

TEST_F(FileBackendTest, KeyWithHyphenAndUnderscore) {
  ASSERT_EQ(SYSPROP_OK, backend_->Set("my-key_name.prop", "value"));
  const int n = backend_->Get("my-key_name.prop", buf_, sizeof(buf_));
  ASSERT_GE(n, 0);
  EXPECT_STREQ("value", buf_);
}

TEST_F(FileBackendTest, LargeValue) {
  const std::string large(SYSPROP_MAX_VALUE_LENGTH - 1, 'x');
  ASSERT_EQ(SYSPROP_OK, backend_->Set("test.large", large.c_str()));
  const int n = backend_->Get("test.large", buf_, sizeof(buf_));
  ASSERT_EQ(SYSPROP_MAX_VALUE_LENGTH - 1, n);
  EXPECT_EQ(large, buf_);
}

TEST_F(FileBackendTest, GetWithExactBuffer) {
  // Buffer is exactly value_len + 1 (tight but valid).
  ASSERT_EQ(SYSPROP_OK, backend_->Set("test.key", "hi"));
  char tight[3] = {};
  const int n = backend_->Get("test.key", tight, sizeof(tight));
  ASSERT_GE(n, 0);
  EXPECT_STREQ("hi", tight);
}

// ── Buffer size edge cases ────────────────────────────────────────────────────

TEST_F(FileBackendTest, GetBufferTooSmallTruncates) {
  ASSERT_EQ(SYSPROP_OK, backend_->Set("test.long", "hello world"));
  // Provide only 4 bytes: output must be null-terminated and not overflow.
  char small_buf[4] = {};
  const int n = backend_->Get("test.long", small_buf, sizeof(small_buf));
  ASSERT_GE(n, 0);
  EXPECT_EQ('\0', small_buf[3]);
}

TEST_F(FileBackendTest, GetNullBuffer) {
  EXPECT_EQ(SYSPROP_ERR_BUFFER_TOO_SMALL, backend_->Get("any.key", nullptr, 0));
}

// ── ForEach ───────────────────────────────────────────────────────────────────

TEST_F(FileBackendTest, ForEachEmpty) {
  int count = 0;
  auto fn = [&](const char*, const char*) {
    ++count;
    return true;
  };
  const int rc = backend_->ForEach(MakeVisitor(fn));
  EXPECT_EQ(SYSPROP_OK, rc);
  EXPECT_EQ(0, count);
}

TEST_F(FileBackendTest, ForEachSeveralKeys) {
  ASSERT_EQ(SYSPROP_OK, backend_->Set("a.key", "1"));
  ASSERT_EQ(SYSPROP_OK, backend_->Set("b.key", "2"));
  ASSERT_EQ(SYSPROP_OK, backend_->Set("c.key", "3"));

  std::vector<std::pair<std::string, std::string>> collected;
  auto fn = [&](const char* k, const char* v) {
    collected.emplace_back(k, v);
    return true;
  };
  (void)backend_->ForEach(MakeVisitor(fn));
  EXPECT_EQ(3u, collected.size());
}

TEST_F(FileBackendTest, ForEachEarlyStop) {
  ASSERT_EQ(SYSPROP_OK, backend_->Set("a.key", "1"));
  ASSERT_EQ(SYSPROP_OK, backend_->Set("b.key", "2"));
  ASSERT_EQ(SYSPROP_OK, backend_->Set("c.key", "3"));

  int count = 0;
  auto fn = [&](const char*, const char*) {
    ++count;
    return false;
  };
  (void)backend_->ForEach(MakeVisitor(fn));
  EXPECT_EQ(1, count);
}

TEST_F(FileBackendTest, ForEachSkipsTmpFiles) {
  // Manually place a stale .tmp.* file in the directory.
  const std::string tmp_file = tmp_dir_ + "/.tmp.stale.123";
  FILE* f = std::fopen(tmp_file.c_str(), "w");
  ASSERT_NE(f, nullptr);
  std::fclose(f);

  ASSERT_EQ(SYSPROP_OK, backend_->Set("real.key", "v"));

  int count = 0;
  auto fn = [&](const char*, const char*) {
    ++count;
    return true;
  };
  (void)backend_->ForEach(MakeVisitor(fn));
  EXPECT_EQ(1, count);  // Only the real key, not the .tmp.* file.
}

// ── Filesystem-attack hardening ───────────────────────────────────────────────

TEST_F(FileBackendTest, GetFromNonexistentBaseDirReturnsError) {
  // Base directory that was never created.
  const std::string ghost = testing::TempDir() + "sysprop_ghost_dir_never_created";
  FileBackend bad{ghost.c_str()};
  char buf[SYSPROP_MAX_VALUE_LENGTH];
  EXPECT_NE(SYSPROP_OK, bad.Get("some.key", buf, sizeof(buf)));
}

TEST_F(FileBackendTest, SetToNonexistentBaseDirReturnsError) {
  const std::string ghost = testing::TempDir() + "sysprop_ghost_dir_never_created";
  FileBackend bad{ghost.c_str()};
  EXPECT_NE(SYSPROP_OK, bad.Set("some.key", "value"));
}

TEST_F(FileBackendTest, GetWhenPropertyFileIsDirectoryReturnsError) {
  // Attacker or previous crash left a directory where a property file is expected.
  const std::string dir_path = tmp_dir_ + "/dir.key";
  ASSERT_EQ(0, ::mkdir(dir_path.c_str(), 0755));
  EXPECT_NE(SYSPROP_OK, backend_->Get("dir.key", buf_, sizeof(buf_)));
}

TEST_F(FileBackendTest, SetAtomicallyReplacesPreplacedSymlink) {
  // Attacker pre-places a symlink in the property store pointing to a sensitive
  // file. Set() must replace the symlink itself, not write through it.
  const std::string target = tmp_dir_ + "/.attacker_target";
  {
    FILE* f = std::fopen(target.c_str(), "w");
    ASSERT_NE(f, nullptr);
    std::fputs("attacker_data", f);
    std::fclose(f);
  }
  ASSERT_EQ(0, ::symlink(target.c_str(), (tmp_dir_ + "/sym.key").c_str()));

  ASSERT_EQ(SYSPROP_OK, backend_->Set("sym.key", "safe_value"));

  // The directory entry must now be a regular file, not a symlink.
  struct stat st {};
  ASSERT_EQ(0, ::lstat((tmp_dir_ + "/sym.key").c_str(), &st));
  EXPECT_TRUE(S_ISREG(st.st_mode)) << "rename() must replace the symlink, not follow it";

  // The attacker's target must be untouched.
  char tbuf[64] = {};
  FILE* f = std::fopen(target.c_str(), "r");
  ASSERT_NE(f, nullptr);
  const size_t nread = std::fread(tbuf, 1, sizeof(tbuf) - 1, f);
  (void)nread;
  std::fclose(f);
  EXPECT_STREQ("attacker_data", tbuf);

  // And the property must return our value.
  const int n = backend_->Get("sym.key", buf_, sizeof(buf_));
  ASSERT_GE(n, 0);
  EXPECT_STREQ("safe_value", buf_);
}

TEST_F(FileBackendTest, GetUnreadablePropertyFileReturnsError) {
  if (::getuid() == 0) {
    GTEST_SKIP() << "root bypasses permission checks";
  }
  ASSERT_EQ(SYSPROP_OK, backend_->Set("perm.key", "secret"));
  ASSERT_EQ(0, ::chmod((tmp_dir_ + "/perm.key").c_str(), 0000));
  EXPECT_NE(SYSPROP_OK, backend_->Get("perm.key", buf_, sizeof(buf_)));
  ::chmod((tmp_dir_ + "/perm.key").c_str(), 0644);  // restore for TearDown
}

TEST_F(FileBackendTest, ForEachSkipsInjectedDotFiles) {
  // Files starting with '.' cannot be created via the validated API, but an
  // attacker with filesystem access could inject them; they must not surface.
  const std::string injected = tmp_dir_ + "/.injected_secret";
  FILE* f = std::fopen(injected.c_str(), "w");
  ASSERT_NE(f, nullptr);
  std::fputs("evil", f);
  std::fclose(f);

  ASSERT_EQ(SYSPROP_OK, backend_->Set("real.key", "v"));

  int count = 0;
  auto fn = [&](const char*, const char*) {
    ++count;
    return true;
  };
  (void)backend_->ForEach(MakeVisitor(fn));
  EXPECT_EQ(1, count);
}

TEST_F(FileBackendTest, ForEachSkipsSubdirectories) {
  // A subdirectory in the store (e.g., from a bug or attack) must be silently
  // skipped, not crash or corrupt iteration.
  ASSERT_EQ(0, ::mkdir((tmp_dir_ + "/subdir.entry").c_str(), 0755));
  ASSERT_EQ(SYSPROP_OK, backend_->Set("real.key", "v"));

  int count = 0;
  auto fn = [&](const char*, const char*) {
    ++count;
    return true;
  };
  (void)backend_->ForEach(MakeVisitor(fn));
  EXPECT_EQ(1, count);
}

TEST_F(FileBackendTest, ForEachWithManyKeys) {
  constexpr int kCount = 200;
  for (int i = 0; i < kCount; ++i) {
    ASSERT_EQ(SYSPROP_OK,
              backend_->Set(("k." + std::to_string(i)).c_str(), std::to_string(i).c_str()));
  }
  int count = 0;
  auto fn = [&](const char*, const char*) {
    ++count;
    return true;
  };
  (void)backend_->ForEach(MakeVisitor(fn));
  EXPECT_EQ(kCount, count);
}

// ── Concurrency ───────────────────────────────────────────────────────────────

TEST_F(FileBackendTest, ConcurrentReadsNoCrash) {
  ASSERT_EQ(SYSPROP_OK, backend_->Set("shared.key", "shared_value"));

  constexpr int kThreads = 8;
  constexpr int kIters = 500;
  std::vector<std::thread> threads;
  threads.reserve(kThreads);

  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&] {
      char local_buf[SYSPROP_MAX_VALUE_LENGTH];
      for (int i = 0; i < kIters; ++i) {
        EXPECT_GE(backend_->Get("shared.key", local_buf, sizeof(local_buf)), 0);
      }
    });
  }
  for (auto& th : threads) th.join();
}

TEST_F(FileBackendTest, ConcurrentWritesNoCrash) {
  constexpr int kThreads = 4;
  constexpr int kIters = 200;
  std::vector<std::thread> threads;
  threads.reserve(kThreads);

  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&, t] {
      const std::string val = "thread" + std::to_string(t);
      for (int i = 0; i < kIters; ++i) {
        (void)backend_->Set("contested.key", val.c_str());
      }
    });
  }
  for (auto& th : threads) th.join();

  char result[SYSPROP_MAX_VALUE_LENGTH];
  EXPECT_GE(backend_->Get("contested.key", result, sizeof(result)), 0);
}

TEST_F(FileBackendTest, ConcurrentReadWriteNoCrash) {
  ASSERT_EQ(SYSPROP_OK, backend_->Set("rw.key", "initial"));

  constexpr int kIters = 300;
  std::thread writer([&] {
    for (int i = 0; i < kIters; ++i) {
      (void)backend_->Set("rw.key", ("v" + std::to_string(i)).c_str());
    }
  });

  std::thread reader([&] {
    char local_buf[SYSPROP_MAX_VALUE_LENGTH];
    for (int i = 0; i < kIters; ++i) {
      (void)backend_->Get("rw.key", local_buf, sizeof(local_buf));
    }
  });

  writer.join();
  reader.join();
}
