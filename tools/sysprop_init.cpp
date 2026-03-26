// sysprop_init — Boot-time system property initializer.
//
// Responsibilities:
//   1. Create the runtime property directory (mkdir -p equivalent).
//   2. Remove stale .tmp.* files left by crashed writers.
//   3. Load persistent properties from the persistent dir into the runtime dir.
//   4. Optionally load a build.prop-style defaults file (key=value per line;
//      lines starting with '#' are comments).
//
// Usage:
//   sysprop-init [--runtime-dir DIR] [--persistent-dir DIR]
//               [--no-persistence] [defaults-file]
//
// Environment overrides (same as the sysprop CLI):
//   SYSPROP_RUNTIME_DIR
//   SYSPROP_PERSISTENT_DIR

#include <dirent.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>

#include <sys/stat.h>
#include <sysprop/sysprop.h>

#include "file_backend.h"
#include "property_store.h"

namespace {

using sysprop::internal::FileBackend;
using sysprop::internal::PropertyStore;

// ── Directory utilities ───────────────────────────────────────────────────────

// mkdir -p: creates path and any missing intermediate directories.
// Returns true on success.
bool MkdirP(const char* path) {
  std::string p{path};
  for (std::size_t i = 1; i <= p.size(); ++i) {
    if (i == p.size() || p[i] == '/') {
      const char saved = p[i];
      p[i] = '\0';
      if (::mkdir(p.c_str(), 0755) < 0 && errno != EEXIST) {
        std::fprintf(stderr, "sysprop-init: mkdir('%s'): %s\n", p.c_str(), std::strerror(errno));
        p[i] = saved;
        return false;
      }
      p[i] = saved;
    }
  }
  return true;
}

// Remove stale .tmp.* files that may have been left by a crashed writer.
void CleanupTmpFiles(const char* dir) {
  DIR* d = ::opendir(dir);
  if (d == nullptr) return;

  const struct dirent* entry;
  while ((entry = ::readdir(d)) != nullptr) {
    if (std::strncmp(entry->d_name, ".tmp.", 5) != 0) continue;

    char path[4096];
    std::snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
    if (::unlink(path) == 0) {
      std::fprintf(stderr, "sysprop-init: removed stale temp file: %s\n", path);
    }
  }
  ::closedir(d);
}

// ── Defaults file parsing ─────────────────────────────────────────────────────

// Parse a build.prop-style file (lines of "key=value"; '#' introduces a
// comment). Returns the number of properties loaded, or -1 on open failure.
int LoadDefaultsFile(const char* path, PropertyStore& store) {
  FILE* f = std::fopen(path, "r");
  if (f == nullptr) {
    std::fprintf(stderr, "sysprop-init: cannot open defaults file '%s': %s\n", path,
                 std::strerror(errno));
    return -1;
  }

  // Enough space for the longest possible "key=value\r\n\0".
  // MAX_KEY_LENGTH and MAX_VALUE_LENGTH include the null terminator, so the
  // actual max string lengths are each (MAX - 1).  Total: (MAX_KEY-1) + 1 +
  // (MAX_VAL-1) + 2 + 1 = MAX_KEY + MAX_VAL + 2.
  char line[SYSPROP_MAX_KEY_LENGTH + SYSPROP_MAX_VALUE_LENGTH + 2];
  int loaded = 0;
  int lineno = 0;

  while (std::fgets(line, sizeof(line), f) != nullptr) {
    ++lineno;

    // Strip trailing CR/LF.
    std::size_t len = std::strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
      line[--len] = '\0';
    }

    // Skip leading whitespace, blank lines, and comments.
    const char* p = line;
    while (*p == ' ' || *p == '\t') ++p;
    if (*p == '\0' || *p == '#') continue;

    // Split on the first '='.
    const char* eq = std::strchr(p, '=');
    if (eq == nullptr) {
      std::fprintf(stderr, "sysprop-init: %s:%d: malformed line (missing '='): %s\n", path, lineno,
                   line);
      continue;
    }

    // Use string_view to avoid an allocation for the key slice.
    const std::string_view key_sv{p, static_cast<std::size_t>(eq - p)};
    const std::string key{key_sv};  // PropertyStore takes const char*
    const char* value = eq + 1;

    if (const int rc = store.Set(key.c_str(), value); rc == SYSPROP_OK) {
      ++loaded;
    } else {
      std::fprintf(stderr, "sysprop-init: %s:%d: failed to set '%s': %s\n", path, lineno,
                   key.c_str(), sysprop_error_string(rc));
    }
  }

  std::fclose(f);
  return loaded;
}

// ── Argument parsing ──────────────────────────────────────────────────────────

struct Args {
  const char* runtime_dir = SYSPROP_RUNTIME_DIR;
  const char* persistent_dir = SYSPROP_PERSISTENT_DIR;
  const char* defaults_file = nullptr;
  bool enable_persistence = true;
};

Args ParseArgs(int argc, char* argv[]) {
  Args args;
  // Environment variables take priority over compiled-in defaults but can be
  // overridden further by explicit command-line flags below.
  if (const char* rd = std::getenv("SYSPROP_RUNTIME_DIR")) args.runtime_dir = rd;
  if (const char* pd = std::getenv("SYSPROP_PERSISTENT_DIR")) args.persistent_dir = pd;

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg{argv[i]};
    if (arg == "--runtime-dir" && i + 1 < argc) {
      args.runtime_dir = argv[++i];
    } else if (arg == "--persistent-dir" && i + 1 < argc) {
      args.persistent_dir = argv[++i];
    } else if (arg == "--no-persistence") {
      args.enable_persistence = false;
    } else if (arg.substr(0, 2) != "--") {
      args.defaults_file = argv[i];
    } else {
      std::fprintf(stderr, "sysprop-init: unknown option: %s\n", argv[i]);
    }
  }
  return args;
}

}  // namespace

// ── main ──────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
  const Args args = ParseArgs(argc, argv);

  // 1. Create runtime directory.
  if (!MkdirP(args.runtime_dir)) return 1;

  // 2. Remove stale temp files from previous crashes.
  CleanupTmpFiles(args.runtime_dir);

  FileBackend runtime_backend{args.runtime_dir};
  std::unique_ptr<FileBackend> persistent_backend;
  if (args.enable_persistence) {
    if (!MkdirP(args.persistent_dir)) return 1;
    persistent_backend = std::make_unique<FileBackend>(args.persistent_dir);
  }

  PropertyStore store{&runtime_backend, persistent_backend.get()};

  // 3. Load persistent properties.
  int total_loaded = 0;
  if (args.enable_persistence && persistent_backend != nullptr) {
    const int n = store.LoadPersistentProperties();
    std::fprintf(stderr, "sysprop-init: loaded %d persistent properties\n", n);
    total_loaded += n;
  }

  // 4. Load defaults file (optional).
  if (args.defaults_file != nullptr) {
    const int n = LoadDefaultsFile(args.defaults_file, store);
    if (n < 0) return 1;
    std::fprintf(stderr, "sysprop-init: loaded %d properties from '%s'\n", n, args.defaults_file);
    total_loaded += n;
  }

  std::fprintf(stderr, "sysprop-init: done (%d properties loaded)\n", total_loaded);
  return 0;
}
