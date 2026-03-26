#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$REPO_ROOT/build"

# Ensure compilation database exists.
cmake -S "$REPO_ROOT" -B "$BUILD_DIR" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON --log-level=ERROR "$@"

# Discover the compiler's implicit include paths by running it with -v.
# This works on plain Linux/macOS and on nix, where the compiler wrapper
# injects nix-store paths that are not recorded in compile_commands.json.
COMPILER="$(which clang++ 2>/dev/null || which c++)"
EXTRA_ARGS=()
while IFS= read -r dir; do
  EXTRA_ARGS+=(--extra-arg="-isystem$dir")
done < <(
  echo | "$COMPILER" -x c++ -std=c++17 -v -E - 2>&1 \
    | awk '/^#include <\.\.\.>/,/^End of search list/' \
    | grep '^ ' \
    | sed 's/^ //'
)

SRC_FILES=(
  "$REPO_ROOT"/src/file_backend.cpp
  "$REPO_ROOT"/src/property_store.cpp
  "$REPO_ROOT"/src/sysprop.cpp
  "$REPO_ROOT"/src/validation.cpp
)

echo "Running clang-tidy on src/..."
clang-tidy -p "$BUILD_DIR" "${EXTRA_ARGS[@]}" "${SRC_FILES[@]}"
echo "clang-tidy passed."
