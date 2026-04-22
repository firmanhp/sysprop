#pragma once

namespace sysprop::tools {

// mkdir -p equivalent. Creates path and all missing intermediate directories
// with mode 0755. Returns true on success, false on failure (error to stderr).
bool MkdirP(const char* path);

// Remove any files whose names begin with ".tmp." from dir. Silently skips
// entries that cannot be removed. No-op if dir cannot be opened.
void CleanupTmpFiles(const char* dir);

// Parsed result of sysprop-init command-line arguments.
struct InitArgs {
  const char* defaults_file = nullptr;
};

// Parse sysprop-init argc/argv. The only recognized argument is an optional
// positional path to a defaults file. Unknown --flags are logged to stderr.
InitArgs ParseInitArgs(int argc, char* argv[]);

}  // namespace sysprop::tools
