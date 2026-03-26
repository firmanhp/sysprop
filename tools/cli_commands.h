#pragma once

namespace sysprop::internal { class PropertyStore; }

namespace sysprop::tools {

// List all properties in the store to stdout, sorted, one "[key]: [value]" per line.
// Always returns 0.
int DoList(sysprop::internal::PropertyStore& store);

// getprop-style get.
//   argc == 1                → list all (delegates to DoList)
//   argc == 2, argv[1]=key  → print value; print "" and return 0 if not found
//   argc >= 3, key not found → print argv[2] (default) and return 0
//   other error on get      → print to stderr, return 1
int CmdGetprop(int argc, char* argv[], sysprop::internal::PropertyStore& store);

// setprop-style set.
//   argc < 3  → print usage to stderr, return 1
//   otherwise → set argv[1]=argv[2]; return 0 on success, 1 on failure
int CmdSetprop(int argc, char* argv[], sysprop::internal::PropertyStore& store);

// sysprop delete.
//   argc < 2  → print usage to stderr, return 1
//   otherwise → delete argv[1]; return 0 on success, 1 on failure
int CmdDelete(int argc, char* argv[], sysprop::internal::PropertyStore& store);

}  // namespace sysprop::tools
