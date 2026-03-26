#include <unistd.h>

#include <cstdio>
#include <memory>
#include <string>

#include <benchmark/benchmark.h>
#include <gtest/gtest.h>
#include <sysprop/sysprop.h>

#include "file_backend.h"
#include "property_store.h"

namespace {

using sysprop::internal::FileBackend;
using sysprop::internal::PropertyStore;

// ── Fixture helpers ───────────────────────────────────────────────────────────

std::string MakeTmpDir() {
  std::string tmpl = testing::TempDir() + "sysprop_bench_XXXXXX";
  const char* d = ::mkdtemp(tmpl.data());
  if (d == nullptr) std::abort();
  return d;
}

// Pre-populate a store with n properties named "bench.key.000000", etc.
void Populate(PropertyStore& store, int n) {
  char key[SYSPROP_MAX_KEY_LENGTH];
  for (int i = 0; i < n; ++i) {
    std::snprintf(key, sizeof(key), "bench.key.%06d", i);
    (void)store.Set(key, "benchmark_value");
  }
}

// ── Benchmarks ────────────────────────────────────────────────────────────────

void BM_Get(benchmark::State& state) {
  const auto dir = MakeTmpDir();
  FileBackend backend{dir.c_str()};
  PropertyStore store{&backend, nullptr};
  (void)store.Set("bench.target", "hello_world");

  char buf[SYSPROP_MAX_VALUE_LENGTH];
  for (auto _ : state) {
    benchmark::DoNotOptimize(store.Get("bench.target", buf, sizeof(buf)));
  }
  state.SetItemsProcessed(state.iterations());

}
BENCHMARK(BM_Get);

void BM_GetMissing(benchmark::State& state) {
  const auto dir = MakeTmpDir();
  FileBackend backend{dir.c_str()};
  PropertyStore store{&backend, nullptr};

  char buf[SYSPROP_MAX_VALUE_LENGTH];
  for (auto _ : state) {
    benchmark::DoNotOptimize(store.Get("no.such.key", buf, sizeof(buf)));
  }
  state.SetItemsProcessed(state.iterations());

}
BENCHMARK(BM_GetMissing);

void BM_Set(benchmark::State& state) {
  const auto dir = MakeTmpDir();
  FileBackend backend{dir.c_str()};
  PropertyStore store{&backend, nullptr};

  for (auto _ : state) {
    benchmark::DoNotOptimize(store.Set("bench.write.key", "benchmark_value"));
  }
  state.SetItemsProcessed(state.iterations());

}
BENCHMARK(BM_Set);

void BM_List(benchmark::State& state) {
  const auto dir = MakeTmpDir();
  FileBackend backend{dir.c_str()};
  PropertyStore store{&backend, nullptr};
  Populate(store, static_cast<int>(state.range(0)));

  for (auto _ : state) {
    int count = 0;
    // Explicitly discard ForEach's return: iteration errors are non-fatal
    // in a list context and the benchmark measures throughput, not error paths.
    (void)store.ForEach([&](const char*, const char*) {
      ++count;
      return true;
    });
    benchmark::DoNotOptimize(count);
  }
  state.SetItemsProcessed(state.iterations() * state.range(0));

}
BENCHMARK(BM_List)->Arg(10)->Arg(100)->Arg(1000);

void BM_ConcurrentReads(benchmark::State& state) {
  // Shared state for the multi-threaded benchmark. Only thread 0 sets up and
  // tears down; all threads execute the inner loop in parallel.
  static std::string shared_dir;  // NOLINT(runtime/string)
  static std::unique_ptr<FileBackend> shared_backend;
  static std::unique_ptr<PropertyStore> shared_store;

  if (state.thread_index() == 0) {
    shared_dir = MakeTmpDir();
    shared_backend = std::make_unique<FileBackend>(shared_dir.c_str());
    shared_store = std::make_unique<PropertyStore>(shared_backend.get(), nullptr);
    (void)shared_store->Set("shared.read.key", "shared_value");
  }

  char buf[SYSPROP_MAX_VALUE_LENGTH];
  for (auto _ : state) {
    benchmark::DoNotOptimize(shared_store->Get("shared.read.key", buf, sizeof(buf)));
  }
  state.SetItemsProcessed(state.iterations());

  if (state.thread_index() == 0) {
    shared_store.reset();
    shared_backend.reset();
  }
}
BENCHMARK(BM_ConcurrentReads)->Threads(1)->Threads(2)->Threads(4)->Threads(8);

}  // namespace
