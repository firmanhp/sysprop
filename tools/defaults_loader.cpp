#include "defaults_loader.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

#include <sysprop/sysprop.h>

#include <sysprop/property_store.h>

namespace sysprop::tools {

int LoadDefaultsFile(const char* path, sysprop::internal::PropertyStore& store) {
  FILE* f = std::fopen(path, "r");
  if (f == nullptr) {
    std::fprintf(stderr, "sysprop-init: cannot open defaults file '%s': %s\n",
                 path,  // NOLINT(cppcoreguidelines-pro-type-vararg)
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

  while (std::fgets(line, sizeof(line), f) !=
         nullptr) {  // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
    ++lineno;

    // Strip trailing CR/LF.
    std::size_t len =
        std::strlen(line);  // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
      line[--len] = '\0';
    }

    // Skip leading whitespace, blank lines, and comments.
    const char* p = line;  // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
    while (*p == ' ' || *p == '\t') {
      ++p;
    }
    if (*p == '\0' || *p == '#') {
      continue;
    }

    // Split on the first '='.
    const char* eq = std::strchr(p, '=');
    if (eq == nullptr) {
      std::fprintf(stderr, "sysprop-init: %s:%d: malformed line (missing '='): %s\n", path,
                   lineno,  // NOLINT(cppcoreguidelines-pro-type-vararg)
                   line);   // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
      continue;
    }

    // The value is everything after the first '=' (may itself contain '=').
    const std::string_view key_sv{p, static_cast<std::size_t>(eq - p)};
    const std::string key{key_sv};
    const char* value = eq + 1;

    if (const int rc = store.Set(key.c_str(), value); rc == SYSPROP_OK) {
      ++loaded;
    } else {
      std::fprintf(stderr, "sysprop-init: %s:%d: failed to set '%s': %s\n", path,
                   lineno,  // NOLINT(cppcoreguidelines-pro-type-vararg)
                   key.c_str(), sysprop_error_string(rc));
    }
  }

  std::fclose(f);
  return loaded;
}

}  // namespace sysprop::tools
