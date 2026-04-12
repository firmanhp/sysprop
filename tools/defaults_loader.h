#pragma once

namespace sysprop::internal {
class PropertyStore;
}

namespace sysprop::tools {

// Parse a build.prop-style defaults file into store.
//
// Format:
//   - One "key=value" pair per line.
//   - Leading whitespace is stripped before parsing.
//   - Lines whose first non-whitespace character is '#' are comments.
//   - Blank lines are skipped.
//   - The value is everything after the first '=' (may itself contain '=').
//   - Windows-style CR+LF line endings are handled.
//
// Returns the number of properties successfully set, or -1 if the file
// could not be opened. Lines that fail validation or store insertion are
// skipped with a warning to stderr; they do not affect the return value.
int LoadDefaultsFile(const char* path, sysprop::internal::PropertyStore& store);

}  // namespace sysprop::tools
