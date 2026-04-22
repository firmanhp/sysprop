// sysprop_main.cpp — Busybox-style CLI for the sysprop library.
//
// Dispatch rules:
//   argv[0] == "getprop"  →  do_getprop
//   argv[0] == "setprop"  →  do_setprop
//   argv[0] == "sysprop"  →  dispatch on argv[1] (get / set / delete)
//
// Environment overrides:
//   SYSPROP_RUNTIME_DIR    — override the runtime property directory
//   SYSPROP_PERSISTENT_DIR — override the persistent property directory

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string_view>

#include <sysprop/sysprop.h>

#include "cli_commands.h"
#include "file_backend.h"
#include "file_property_store.h"

namespace {

using sysprop::internal::FileBackend;
using sysprop::internal::FilePropertyStore;

const char* ProgName(const char* argv0) noexcept {
  const char* p = std::strrchr(argv0, '/');
  return p != nullptr ? p + 1 : argv0;
}

struct ToolConfig {
  const char* runtime_dir    = SYSPROP_RUNTIME_DIR;
  const char* persistent_dir = SYSPROP_PERSISTENT_DIR;
  bool enable_persistence    = true;
};

ToolConfig ConfigFromEnv() {
  ToolConfig cfg;
  if (const char* rd = std::getenv("SYSPROP_RUNTIME_DIR")) {
    cfg.runtime_dir = rd;
  }
  if (const char* pd = std::getenv("SYSPROP_PERSISTENT_DIR")) {
    cfg.persistent_dir = pd;
  }
  return cfg;
}

void PrintUsage(const char* prog) {
  std::fprintf(stderr,
               "Usage:\n"
               "  %s get key [default]  -- print property value\n"
               "  %s set key value      -- set a property\n"
               "  %s delete key         -- delete a property\n"
               "\n"
               "Environment:\n"
               "  SYSPROP_RUNTIME_DIR     override runtime directory    (default: %s)\n"
               "  SYSPROP_PERSISTENT_DIR  override persistent directory (default: %s)\n",
               prog, prog, prog, SYSPROP_RUNTIME_DIR, SYSPROP_PERSISTENT_DIR);
}

}  // namespace

// ── main ──────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
  const char* prog = ProgName(argv[0]);
  const ToolConfig cfg = ConfigFromEnv();

  // Construct backends directly rather than using the global singleton so that
  // environment-variable overrides take effect per invocation.
  FileBackend runtime_backend(cfg.runtime_dir);
  std::unique_ptr<FileBackend> persistent_backend;
  if (cfg.enable_persistence) {
    persistent_backend = std::make_unique<FileBackend>(cfg.persistent_dir);
  }
  FilePropertyStore store(&runtime_backend, persistent_backend.get());

  const std::string_view name{prog};

  if (name == "getprop") return sysprop::tools::CmdGetprop(argc, argv, store);
  if (name == "setprop") return sysprop::tools::CmdSetprop(argc, argv, store);

  // Invoked as "sysprop <subcommand> ..."
  if (argc < 2) {
    PrintUsage(prog);
    return 1;
  }

  const std::string_view cmd{argv[1]};
  if (cmd == "get") return sysprop::tools::CmdGetprop(argc - 1, argv + 1, store);
  if (cmd == "set") return sysprop::tools::CmdSetprop(argc - 1, argv + 1, store);
  if (cmd == "delete") return sysprop::tools::CmdDelete(argc - 1, argv + 1, store);

  std::fprintf(stderr, "sysprop: unknown command '%s'\n", argv[1]);
  PrintUsage(prog);
  return 1;
}
