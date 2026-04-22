// sysprop_main.cpp — Busybox-style CLI for the sysprop library.
//
// Dispatch rules:
//   argv[0] == "getprop"  →  do_getprop
//   argv[0] == "setprop"  →  do_setprop
//   argv[0] == "sysprop"  →  dispatch on argv[1] (get / set / delete)
//
// Directory paths and persistence are baked in at build time via CMake cache
// variables: SYSPROP_RUNTIME_DIR, SYSPROP_PERSISTENT_DIR,
// SYSPROP_ENABLE_PERSISTENCE.

#include <cstdio>
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

void PrintUsage(const char* prog) {
  std::fprintf(stderr,
               "Usage:\n"
               "  %s get key [default]  -- print property value\n"
               "  %s set key value      -- set a property\n"
               "  %s delete key         -- delete a property\n",
               prog, prog, prog);
}

}  // namespace

// ── main ──────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
  const char* prog = ProgName(argv[0]);

  FileBackend runtime_backend(SYSPROP_RUNTIME_DIR);
  std::unique_ptr<FileBackend> persistent_backend;
  if (SYSPROP_ENABLE_PERSISTENCE) {
    persistent_backend = std::make_unique<FileBackend>(SYSPROP_PERSISTENT_DIR);
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
