#include "cli_commands.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <sysprop/sysprop.h>

#include "property_store.h"

namespace sysprop::tools {

int DoList(sysprop::internal::PropertyStore& store) {
  std::vector<std::pair<std::string, std::string>> props;
  (void)store.ForEach([&](const char* key, const char* value) {
    props.emplace_back(key, value);
    return true;
  });
  std::sort(props.begin(), props.end());
  for (const auto& [k, v] : props) {
    std::printf("[%s]: [%s]\n", k.c_str(), v.c_str()); // NOLINT(cppcoreguidelines-pro-type-vararg)
  }
  return 0;
}

int CmdGetprop(int argc, char* argv[], sysprop::internal::PropertyStore& store) {
  if (argc == 1) { return DoList(store); }

  const char* key = argv[1];
  char buf[SYSPROP_MAX_VALUE_LENGTH];
  const int n = store.Get(key, buf, sizeof(buf)); // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)

  if (n < 0) {
    if (argc >= 3) {
      std::puts(argv[2]);
      return 0;
    }
    if (n == SYSPROP_ERR_NOT_FOUND) {
      std::puts("");
      return 0;
    }
    std::fprintf(stderr, "getprop: error reading '%s': %s\n", key, sysprop_error_string(n)); // NOLINT(cppcoreguidelines-pro-type-vararg)
    return 1;
  }

  std::puts(buf); // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
  return 0;
}

int CmdSetprop(int argc, char* argv[], sysprop::internal::PropertyStore& store) {
  if (argc < 3) {
    std::fprintf(stderr, "Usage: setprop <key> <value>\n"); // NOLINT(cppcoreguidelines-pro-type-vararg)
    return 1;
  }
  if (const int rc = store.Set(argv[1], argv[2]); rc != SYSPROP_OK) {
    std::fprintf(stderr, "setprop: failed to set '%s': %s\n", argv[1], sysprop_error_string(rc)); // NOLINT(cppcoreguidelines-pro-type-vararg)
    return 1;
  }
  return 0;
}

int CmdDelete(int argc, char* argv[], sysprop::internal::PropertyStore& store) {
  if (argc < 2) {
    std::fprintf(stderr, "Usage: sysprop delete <key>\n"); // NOLINT(cppcoreguidelines-pro-type-vararg)
    return 1;
  }
  if (const int rc = store.Delete(argv[1]); rc != SYSPROP_OK) {
    std::fprintf(stderr, "sysprop delete: failed to delete '%s': %s\n", argv[1], sysprop_error_string(rc)); // NOLINT(cppcoreguidelines-pro-type-vararg)
    return 1;
  }
  return 0;
}

}  // namespace sysprop::tools
