#include "defaults_loader.h"

#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include <sysprop/sysprop.h>

#include <sysprop/property_store.h>

namespace sysprop::tools {

int LoadDefaultsFile(const char* path, sysprop::internal::PropertyStore& store) {
  std::ifstream f{path};
  if (!f.is_open()) {
    std::cerr << "sysprop-init: cannot open defaults file '" << path << "': "
              << std::strerror(errno) << '\n';
    return -1;
  }

  int loaded = 0;
  int lineno = 0;
  std::string line;

  while (std::getline(f, line)) {
    ++lineno;

    // Strip trailing CR (getline already strips \n).
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

    // Skip leading whitespace, blank lines, and comments.
    std::size_t start = 0;
    while (start < line.size() && (line[start] == ' ' || line[start] == '\t')) {
      ++start;
    }
    if (start == line.size() || line[start] == '#') {
      continue;
    }

    // Split on the first '='.
    const std::size_t eq_pos = line.find('=', start);
    if (eq_pos == std::string::npos) {
      std::cerr << "sysprop-init: " << path << ':' << lineno
                << ": malformed line (missing '='): " << std::string_view{line}.substr(start)
                << '\n';
      continue;
    }

    // The value is everything after the first '=' (may itself contain '=').
    const std::string key{line, start, eq_pos - start};
    const std::string value{line, eq_pos + 1};

    if (const int rc = store.Set(key.c_str(), value.c_str()); rc == SYSPROP_OK) {
      ++loaded;
    } else {
      std::cerr << "sysprop-init: " << path << ':' << lineno << ": failed to set '" << key
                << "': " << sysprop_error_string(rc) << '\n';
    }
  }

  return loaded;
}

}  // namespace sysprop::tools
