#include "cli_commands.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#include <sysprop/sysprop.h>

#include <sysprop/property_store.h>

namespace sysprop::tools {

int DoList(sysprop::internal::PropertyStore& store) {
  std::vector<std::pair<std::string, std::string>> props;
  (void)store.ForEach([&](const char* key, const char* value) {
    props.emplace_back(key, value);
    return true;
  });
  std::sort(props.begin(), props.end());
  for (const auto& [k, v] : props) {
    std::cout << '[' << k << "]: [" << v << "]\n";
  }
  return 0;
}

int CmdGetprop(int argc, char* argv[], sysprop::internal::PropertyStore& store) {
  if (argc == 1) {
    return DoList(store);
  }

  const char* key = argv[1];
  char buf[SYSPROP_MAX_VALUE_LENGTH];
  const int n = store.Get(key, buf, sizeof(buf));

  if (n < 0) {
    if (argc >= 3) {
      std::cout << argv[2] << '\n';
      return 0;
    }
    if (n == SYSPROP_ERR_NOT_FOUND) {
      std::cout << '\n';
      return 0;
    }
    std::cerr << "getprop: error reading '" << key << "': " << sysprop_error_string(n) << '\n';
    return 1;
  }

  std::cout << buf << '\n';
  return 0;
}

int CmdSetprop(int argc, char* argv[], sysprop::internal::PropertyStore& store) {
  if (argc < 3) {
    std::cerr << "Usage: setprop <key> <value>\n";
    return 1;
  }
  if (const int rc = store.Set(argv[1], argv[2]); rc != SYSPROP_OK) {
    std::cerr << "setprop: failed to set '" << argv[1] << "': " << sysprop_error_string(rc) << '\n';
    return 1;
  }
  return 0;
}

int CmdDelete(int argc, char* argv[], sysprop::internal::PropertyStore& store) {
  if (argc < 2) {
    std::cerr << "Usage: sysprop delete <key>\n";
    return 1;
  }
  if (const int rc = store.Delete(argv[1]); rc != SYSPROP_OK) {
    std::cerr << "sysprop delete: failed to delete '" << argv[1] << "': " << sysprop_error_string(rc) << '\n';
    return 1;
  }
  return 0;
}

}  // namespace sysprop::tools
