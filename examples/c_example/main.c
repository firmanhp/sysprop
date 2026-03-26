// C example — demonstrates the sysprop C API.
//
// Build and run:
//   cmake -B build && cmake --build build
//   ./build/examples/example_c

#include <stdio.h>

#include <sysprop/sysprop.h>

int main(void) {
  // 1. Initialize with a temp directory so the example runs without root.
  sysprop_config_t cfg;
  cfg.runtime_dir        = "/tmp/sysprop-c-example/runtime";
  cfg.persistent_dir     = "/tmp/sysprop-c-example/persistent";
  cfg.enable_persistence = 1;

  int rc = sysprop_init(&cfg);
  if (rc != SYSPROP_OK) {
    fprintf(stderr, "sysprop_init: %s\n", sysprop_error_string(rc));
    return 1;
  }

  // 2. Set a few properties.
  sysprop_set("device.name", "my-board");
  sysprop_set("persist.wifi.enabled", "1");
  sysprop_set("ro.build.version", "1.0.0");

  // 3. Read them back.
  char buf[SYSPROP_MAX_VALUE_LENGTH];

  rc = sysprop_get("device.name", buf, sizeof(buf));
  printf("device.name            = %s\n", rc >= 0 ? buf : sysprop_error_string(rc));

  printf("persist.wifi.enabled   = %s\n",
         sysprop_get_bool("persist.wifi.enabled", 0) ? "true" : "false");

  rc = sysprop_get("ro.build.version", buf, sizeof(buf));
  printf("ro.build.version       = %s\n", rc >= 0 ? buf : sysprop_error_string(rc));

  // 4. ro.* properties are write-once.
  rc = sysprop_set("ro.build.version", "2.0.0");
  printf("set ro.build.version again: %s\n", sysprop_error_string(rc));

  // 5. Delete a property.
  rc = sysprop_delete("device.name");
  printf("delete device.name:    %s\n", sysprop_error_string(rc));

  rc = sysprop_get("device.name", buf, sizeof(buf));
  printf("get device.name after delete: %s\n", sysprop_error_string(rc));

  return 0;
}
