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

// ── Fixture ───────────────────────────────────────────────────────────────────

class FileBackendTest : public ::testing::Test {
 protected:
  void SetUp() override {
    char tmpl[] = "/tmp/sysprop_test_XXXXXX";
    const char* dir = ::mkdtemp(tmpl);
    ASSERT_NE(dir, nullptr) << "mkdtemp failed: " << strerror(errno);
    tmp_dir_ = dir;
    backend_ = std::make_unique<FileBackend>(tmp_dir_);
  }

  void TearDown() override {
    backend_.reset();
    (void)::system(("rm -rf " + tmp_dir_).c_str());
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
  const int rc = backend_->ForEach([&](const char*, const char*) {
    ++count;
    return true;
  });
  EXPECT_EQ(SYSPROP_OK, rc);
  EXPECT_EQ(0, count);
}

TEST_F(FileBackendTest, ForEachSeveralKeys) {
  ASSERT_EQ(SYSPROP_OK, backend_->Set("a.key", "1"));
  ASSERT_EQ(SYSPROP_OK, backend_->Set("b.key", "2"));
  ASSERT_EQ(SYSPROP_OK, backend_->Set("c.key", "3"));

  std::vector<std::pair<std::string, std::string>> collected;
  (void)backend_->ForEach([&](const char* k, const char* v) {
    collected.emplace_back(k, v);
    return true;
  });
  EXPECT_EQ(3u, collected.size());
}

TEST_F(FileBackendTest, ForEachEarlyStop) {
  ASSERT_EQ(SYSPROP_OK, backend_->Set("a.key", "1"));
  ASSERT_EQ(SYSPROP_OK, backend_->Set("b.key", "2"));
  ASSERT_EQ(SYSPROP_OK, backend_->Set("c.key", "3"));

  int count = 0;
  (void)backend_->ForEach([&](const char*, const char*) {
    ++count;
    return false;  // stop after first entry
  });
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
  (void)backend_->ForEach([&](const char*, const char*) {
    ++count;
    return true;
  });
  EXPECT_EQ(1, count);  // Only the real key, not the .tmp.* file.
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
