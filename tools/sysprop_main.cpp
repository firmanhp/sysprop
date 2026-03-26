// sysprop_main.cpp — Busybox-style CLI for the sysprop library.
//
// Dispatch rules:
//   argv[0] == "getprop"  →  do_getprop
//   argv[0] == "setprop"  →  do_setprop
//   argv[0] == "sysprop"  →  dispatch on argv[1] (get / set / delete / list)
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
#include "property_store.h"

namespace {

using sysprop::internal::FileBackend;
using sysprop::internal::PropertyStore;

const char* ProgName(const char* argv0) noexcept {
  const char* p = std::strrchr(argv0, '/');
  return p != nullptr ? p + 1 : argv0;
}

sysprop_config_t ConfigFromEnv() {
  sysprop_config_t cfg{SYSPROP_RUNTIME_DIR, SYSPROP_PERSISTENT_DIR, 1};
  if (const char* rd = std::getenv("SYSPROP_RUNTIME_DIR")) { cfg.runtime_dir = rd; }
  if (const char* pd = std::getenv("SYSPROP_PERSISTENT_DIR")) { cfg.persistent_dir = pd; }
  return cfg;
}

void PrintUsage(const char* prog) {
  std::fprintf(stderr,
               "Usage:\n"
               "  %s get [key] [default]  -- print property (or all if no key)\n"
               "  %s set key value        -- set a property\n"
               "  %s delete key           -- delete a property\n"
               "  %s list                 -- list all properties\n"
               "\n"
               "Environment:\n"
               "  SYSPROP_RUNTIME_DIR     override runtime directory    (default: %s)\n"
               "  SYSPROP_PERSISTENT_DIR  override persistent directory (default: %s)\n",
               prog, prog, prog, prog, SYSPROP_RUNTIME_DIR, SYSPROP_PERSISTENT_DIR);
}

}  // namespace

// ── main ──────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
  const char* prog = ProgName(argv[0]);
  const sysprop_config_t cfg = ConfigFromEnv();

  // Construct backends directly rather than using the global singleton so that
  // environment-variable overrides take effect per invocation.
  FileBackend runtime_backend(cfg.runtime_dir);
  std::unique_ptr<FileBackend> persistent_backend;
  if (cfg.enable_persistence) {
    persistent_backend = std::make_unique<FileBackend>(cfg.persistent_dir);
  }
  PropertyStore store(&runtime_backend, persistent_backend.get());

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
  if (cmd == "list") return sysprop::tools::DoList(store);

  std::fprintf(stderr, "sysprop: unknown command '%s'\n", argv[1]);
  PrintUsage(prog);
  return 1;
}
