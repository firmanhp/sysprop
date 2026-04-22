// sysprop_init — Boot-time system property initializer.
//
// Responsibilities:
//   1. Create the runtime property directory (mkdir -p equivalent).
//   2. Remove stale .tmp.* files left by crashed writers.
//   3. Optionally load a build.prop-style defaults file (key=value per line;
//      lines starting with '#' are comments).
//
// Usage:
//   sysprop-init [defaults-file]
//
// Directory paths are baked in at build time via CMake cache variables:
// SYSPROP_RUNTIME_DIR, SYSPROP_PERSISTENT_DIR. Persistence is enabled when
// SYSPROP_PERSISTENT_DIR is non-empty.

#include <cstdio>
#include <memory>

#include <sysprop/sysprop.h>

#include "defaults_loader.h"
#include "file_backend.h"
#include "init_helpers.h"
#include "file_property_store.h"

using sysprop::internal::FileBackend;
using sysprop::internal::FilePropertyStore;
using sysprop::tools::CleanupTmpFiles;
using sysprop::tools::InitArgs;
using sysprop::tools::MkdirP;
using sysprop::tools::ParseInitArgs;

// ── main ──────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
  const InitArgs args = ParseInitArgs(argc, argv);

  // 1. Create runtime directory.
  if (!MkdirP(SYSPROP_RUNTIME_DIR)) return 1;

  // 2. Remove stale temp files from previous crashes.
  CleanupTmpFiles(SYSPROP_RUNTIME_DIR);

  FileBackend runtime_backend{SYSPROP_RUNTIME_DIR};
  std::unique_ptr<FileBackend> persistent_backend;
  if (SYSPROP_ENABLE_PERSISTENCE) {
    if (!MkdirP(SYSPROP_PERSISTENT_DIR)) return 1;
    persistent_backend = std::make_unique<FileBackend>(SYSPROP_PERSISTENT_DIR);
  }

  FilePropertyStore store{&runtime_backend, persistent_backend.get()};

  // 3. Load defaults file (optional).
  int total_loaded = 0;
  if (args.defaults_file != nullptr) {
    const int n = sysprop::tools::LoadDefaultsFile(args.defaults_file, store);
    if (n < 0) return 1;
    std::fprintf(stderr, "sysprop-init: loaded %d properties from '%s'\n", n, args.defaults_file);
    total_loaded += n;
  }

  std::fprintf(stderr, "sysprop-init: done (%d properties loaded)\n", total_loaded);
  return 0;
}
