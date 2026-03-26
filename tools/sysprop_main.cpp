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

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include <sysprop/sysprop.h>

#include "file_backend.h"
#include "property_store.h"

namespace {

using sysprop::internal::FileBackend;
using sysprop::internal::PropertyStore;

// ── Helpers ───────────────────────────────────────────────────────────────────

const char* ProgName(const char* argv0) noexcept {
  const char* p = std::strrchr(argv0, '/');
  return p != nullptr ? p + 1 : argv0;
}

// Build a config from environment overrides, falling back to compiled defaults.
sysprop_config_t ConfigFromEnv() {
  sysprop_config_t cfg{SYSPROP_RUNTIME_DIR, SYSPROP_PERSISTENT_DIR, 1};
  if (const char* rd = std::getenv("SYSPROP_RUNTIME_DIR")) cfg.runtime_dir = rd;
  if (const char* pd = std::getenv("SYSPROP_PERSISTENT_DIR")) cfg.persistent_dir = pd;
  return cfg;
}

// Print all properties sorted in Android-style "[key]: [value]" format.
int DoList(PropertyStore& store) {
  std::vector<std::pair<std::string, std::string>> props;
  // Discard ForEach's error return: partial listing failure is non-fatal for
  // an interactive list command.
  (void)store.ForEach([&](const char* key, const char* value) {
    props.emplace_back(key, value);
    return true;
  });

  std::sort(props.begin(), props.end());
  for (const auto& [k, v] : props) {
    std::printf("[%s]: [%s]\n", k.c_str(), v.c_str());
  }
  return 0;
}

// ── Sub-commands ──────────────────────────────────────────────────────────────

int CmdGetprop(int argc, char* argv[], PropertyStore& store) {
  if (argc == 1) return DoList(store);

  const char* key = argv[1];
  char buf[SYSPROP_MAX_VALUE_LENGTH];
  const int n = store.Get(key, buf, sizeof(buf));

  if (n < 0) {
    if (argc >= 3) {
      // Optional default value supplied as the second argument.
      std::puts(argv[2]);
      return 0;
    }
    if (n == SYSPROP_ERR_NOT_FOUND) {
      // Android-compatible: a missing key prints an empty line.
      std::puts("");
      return 0;
    }
    std::fprintf(stderr, "getprop: error reading '%s': %s\n", key, sysprop_error_string(n));
    return 1;
  }

  std::puts(buf);
  return 0;
}

int CmdSetprop(int argc, char* argv[], PropertyStore& store) {
  if (argc < 3) {
    std::fprintf(stderr, "Usage: setprop <key> <value>\n");
    return 1;
  }

  if (const int rc = store.Set(argv[1], argv[2]); rc != SYSPROP_OK) {
    std::fprintf(stderr, "setprop: failed to set '%s': %s\n", argv[1], sysprop_error_string(rc));
    return 1;
  }
  return 0;
}

int CmdDelete(int argc, char* argv[], PropertyStore& store) {
  if (argc < 2) {
    std::fprintf(stderr, "Usage: sysprop delete <key>\n");
    return 1;
  }

  if (const int rc = store.Delete(argv[1]); rc != SYSPROP_OK) {
    std::fprintf(stderr, "sysprop delete: failed to delete '%s': %s\n", argv[1],
                 sysprop_error_string(rc));
    return 1;
  }
  return 0;
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

  if (name == "getprop") return CmdGetprop(argc, argv, store);
  if (name == "setprop") return CmdSetprop(argc, argv, store);

  // Invoked as "sysprop <subcommand> ..."
  if (argc < 2) {
    PrintUsage(prog);
    return 1;
  }

  const std::string_view cmd{argv[1]};
  if (cmd == "get") return CmdGetprop(argc - 1, argv + 1, store);
  if (cmd == "set") return CmdSetprop(argc - 1, argv + 1, store);
  if (cmd == "delete") return CmdDelete(argc - 1, argv + 1, store);
  if (cmd == "list") return DoList(store);

  std::fprintf(stderr, "sysprop: unknown command '%s'\n", argv[1]);
  PrintUsage(prog);
  return 1;
}
