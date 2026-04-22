#include "init_helpers.h"

#include <dirent.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>
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
        std::cerr << "sysprop-init: mkdir('" << p << "'): " << std::strerror(errno) << '\n';
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
    if (std::string_view{entry->d_name}.compare(0, 5, ".tmp.") != 0) {
      continue;
    }

    const std::string path = std::string(dir) + "/" + entry->d_name;
    if (::unlink(path.c_str()) == 0) {
      std::cerr << "sysprop-init: removed stale temp file: " << path << '\n';
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
      std::cerr << "sysprop-init: unknown option: " << argv[i] << '\n';
    }
  }
  return args;
}

}  // namespace sysprop::tools
