#pragma once

#include <sysprop/sysprop.h>

namespace sysprop::tools {

// mkdir -p equivalent. Creates path and all missing intermediate directories
// with mode 0755. Returns true on success, false on failure (error to stderr).
bool MkdirP(const char* path);

// Remove any files whose names begin with ".tmp." from dir. Silently skips
// entries that cannot be removed. No-op if dir cannot be opened.
void CleanupTmpFiles(const char* dir);

// Parsed result of sysprop-init command-line arguments.
struct InitArgs {
  const char* runtime_dir    = SYSPROP_RUNTIME_DIR;
  const char* persistent_dir = SYSPROP_PERSISTENT_DIR;
  const char* defaults_file  = nullptr;
  bool enable_persistence    = true;
};

// Parse sysprop-init argc/argv. Environment variables SYSPROP_RUNTIME_DIR and
// SYSPROP_PERSISTENT_DIR are read first, then explicit flags override them.
// Unknown --flags are logged to stderr and ignored.
InitArgs ParseInitArgs(int argc, char* argv[]);

}  // namespace sysprop::tools
