// C++ example — demonstrates the sysprop C API from a C++ translation unit.
//
// Build and run:
//   cmake -B build -DSYSPROP_RUNTIME_DIR=/tmp/sysprop-example && cmake --build build -t example_cpp
//   ./build/examples/example_cpp

#include <array>
#include <cstdio>

#include <sysprop/sysprop.h>

int main() {
  // Library auto-initializes before main() using SYSPROP_RUNTIME_DIR.

  // 1. Set a few properties.
  sysprop_set("device.name", "my-board");
  sysprop_set("persist.wifi.ssid", "HomeNetwork");
  sysprop_set("ro.build.version", "1.0.0");

  // 2. Read them back using a stack-allocated buffer.
  std::array<char, SYSPROP_MAX_VALUE_LENGTH> buf{};

  if (sysprop_get("device.name", buf.data(), buf.size()) >= 0)
    std::printf("device.name          = %s\n", buf.data());

  if (sysprop_get("persist.wifi.ssid", buf.data(), buf.size()) >= 0)
    std::printf("persist.wifi.ssid    = %s\n", buf.data());

  if (sysprop_get("ro.build.version", buf.data(), buf.size()) >= 0)
    std::printf("ro.build.version     = %s\n", buf.data());

  // 3. Typed helpers return a default when the key is missing.
  std::printf("missing.int          = %d\n", sysprop_get_int("missing.int", 42));
  std::printf("missing.bool         = %s\n",
              sysprop_get_bool("missing.bool", 0) ? "true" : "false");
  std::printf("missing.float        = %.2f\n", sysprop_get_float("missing.float", 3.14f));

  // 4. ro.* properties are write-once.
  const int rc = sysprop_set("ro.build.version", "2.0.0");
  std::printf("set ro.build.version again: %s\n", sysprop_error_string(rc));

  return 0;
}
