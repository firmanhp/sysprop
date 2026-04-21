#pragma once  // NOLINT(llvm-header-guard) -- #pragma once is used throughout; llvm-header-guard
              // requires #ifndef-style guards

namespace sysprop::internal {

class PropertyStore;

// Replace the active global property store. Returns the previous store pointer
// (nullptr if none was set). Pass nullptr to clear the override and fall back
// to the singleton created by sysprop_init().
//
// Not thread-safe. Intended for single-threaded test setup/teardown only.
PropertyStore* swap_store(PropertyStore* new_store);

}  // namespace sysprop::internal
