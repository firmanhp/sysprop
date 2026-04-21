// C example — demonstrates the sysprop C API.
//
// Build and run:
//   cmake -B build -DSYSPROP_RUNTIME_DIR=/tmp/sysprop-example && cmake --build build
//   ./build/examples/example_c

#include <stdio.h>

#include <sysprop/sysprop.h>

int main(void) {
  /* Library auto-initializes before main() using SYSPROP_RUNTIME_DIR. */

  // 1. Set a few properties.
  sysprop_set("device.name", "my-board");
  sysprop_set("persist.wifi.enabled", "1");
  sysprop_set("ro.build.version", "1.0.0");

  // 2. Read them back.
  char buf[SYSPROP_MAX_VALUE_LENGTH];
  int rc;

  rc = sysprop_get("device.name", buf, sizeof(buf));
  printf("device.name            = %s\n", rc >= 0 ? buf : sysprop_error_string(rc));

  printf("persist.wifi.enabled   = %s\n",
         sysprop_get_bool("persist.wifi.enabled", 0) ? "true" : "false");

  rc = sysprop_get("ro.build.version", buf, sizeof(buf));
  printf("ro.build.version       = %s\n", rc >= 0 ? buf : sysprop_error_string(rc));

  // 3. ro.* properties are write-once.
  rc = sysprop_set("ro.build.version", "2.0.0");
  printf("set ro.build.version again: %s\n", sysprop_error_string(rc));

  // 4. Delete a property.
  rc = sysprop_delete("device.name");
  printf("delete device.name:    %s\n", sysprop_error_string(rc));

  rc = sysprop_get("device.name", buf, sizeof(buf));
  printf("get device.name after delete: %s\n", sysprop_error_string(rc));

  return 0;
}
