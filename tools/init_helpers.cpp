#include "init_helpers.h"

#include <dirent.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include <sys/stat.h>

namespace sysprop::tools {

bool MkdirP(const char* path) {
  std::string p{path};
  for (std::size_t i = 1; i <= p.size(); ++i) {
    if (i == p.size() || p[i] == '/') {
      const char saved = p[i];
      p[i] = '\0';
      if (::mkdir(p.c_str(), 0755) < 0 && errno != EEXIST) {
        std::fprintf(stderr, "sysprop-init: mkdir('%s'): %s\n", p.c_str(),
                     std::strerror(errno));  // NOLINT(cppcoreguidelines-pro-type-vararg)
        p[i] = saved;
        return false;
      }
      p[i] = saved;
    }
  }
  return true;
}

void CleanupTmpFiles(const char* dir) {
  DIR* d = ::opendir(dir);
  if (d == nullptr) {
    return;
  }

  const struct dirent* entry = nullptr;
  while ((entry = ::readdir(d)) != nullptr) {
    if (std::strncmp(entry->d_name, ".tmp.", 5) != 0) {
      continue;
    }  // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)

    char path[4096];
    std::snprintf(
        path, sizeof(path), "%s/%s", dir,
        entry
            ->d_name);  // NOLINT(cppcoreguidelines-pro-type-vararg,cppcoreguidelines-pro-bounds-array-to-pointer-decay)
    if (::unlink(path) == 0) {  // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
      std::fprintf(
          stderr, "sysprop-init: removed stale temp file: %s\n",
          path);  // NOLINT(cppcoreguidelines-pro-type-vararg,cppcoreguidelines-pro-bounds-array-to-pointer-decay)
    }
  }
  ::closedir(d);
}

InitArgs ParseInitArgs(int argc, char* argv[]) {
  InitArgs args;
  if (const char* rd = std::getenv("SYSPROP_RUNTIME_DIR")) {
    args.runtime_dir = rd;
  }  // NOLINT(concurrency-mt-unsafe)
  if (const char* pd = std::getenv("SYSPROP_PERSISTENT_DIR")) {
    args.persistent_dir = pd;
  }  // NOLINT(concurrency-mt-unsafe)

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg{argv[i]};  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    if (arg == "--runtime-dir" && i + 1 < argc) {
      args.runtime_dir = argv[++i];  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    } else if (arg == "--persistent-dir" && i + 1 < argc) {
      args.persistent_dir = argv[++i];  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    } else if (arg == "--no-persistence") {
      args.enable_persistence = false;
    } else if (arg.substr(0, 2) != "--") {
      args.defaults_file = argv[i];  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    } else {
      std::fprintf(
          stderr, "sysprop-init: unknown option: %s\n",
          argv[i]);  // NOLINT(cppcoreguidelines-pro-type-vararg,cppcoreguidelines-pro-bounds-pointer-arithmetic)
    }
  }
  return args;
}

}  // namespace sysprop::tools
