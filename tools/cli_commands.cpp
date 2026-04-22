#include "cli_commands.h"

#include <iostream>
#include <map>
#include <string>

#include <sysprop/sysprop.h>

#include <sysprop/property_store.h>

namespace sysprop::tools {

int CmdGetprop(int argc, char* argv[], sysprop::internal::PropertyStore& store) {
  if (argc < 2) {
    std::cerr << "Usage: getprop <key> [default]\n";
    return 1;
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

  const char* key = argv[1];
  std::string value{argv[2]};

  // Strip surrounding double-quotes (e.g. setprop key "value" passes literal quotes in some shells).
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
    value = value.substr(1, value.size() - 2);
  }

  // Empty value means delete.
  if (value.empty()) {
    const int rc = store.Delete(key);
    if (rc != SYSPROP_OK && rc != SYSPROP_ERR_NOT_FOUND) {
      std::cerr << "setprop: failed to delete '" << key << "': " << sysprop_error_string(rc) << '\n';
      return 1;
    }
    return 0;
  }

  if (const int rc = store.Set(key, value.c_str()); rc != SYSPROP_OK) {
    std::cerr << "setprop: failed to set '" << key << "': " << sysprop_error_string(rc) << '\n';
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

int CmdList(sysprop::internal::PropertyStore& store) {
  std::map<std::string, std::string> props;
  const int rc = store.ForEach([&props](const char* key, const char* value) {
    props[key] = value;
  });
  if (rc != SYSPROP_OK) {
    std::cerr << "list: error reading properties: " << sysprop_error_string(rc) << '\n';
    return 1;
  }
  for (const auto& [key, value] : props) {
    std::cout << key << '=' << value << '\n';
  }
  return 0;
}

}  // namespace sysprop::tools
