#pragma once

namespace sysprop::internal { class PropertyStore; }

namespace sysprop::testing {

// Replace the active global property store. Returns the previous store pointer
// (nullptr if none was set). Pass nullptr to clear the override and fall back
// to the auto-initialized singleton.
//
// Not thread-safe. Intended for single-threaded test setup/teardown only.
sysprop::internal::PropertyStore* swap_store(sysprop::internal::PropertyStore* new_store);

}  // namespace sysprop::testing
