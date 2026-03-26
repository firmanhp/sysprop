#!/usr/bin/env bash
# build.sh — Configure and build the sysprop library and CLI tools.
#
# Usage:
#   ./scripts/build.sh [cmake-extra-args...]
#
# Examples:
#   ./scripts/build.sh
#   ./scripts/build.sh -DSYSPROP_RUNTIME_DIR=/tmp/myprops

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$REPO_ROOT/build"

cmake -B "$BUILD_DIR" \
    -DSYSPROP_BUILD_TOOLS=ON \
    "$@" \
    "$REPO_ROOT"

cmake --build "$BUILD_DIR" --parallel "$(nproc 2>/dev/null || sysctl -n hw.logicalcpu)"

echo ""
echo "Build complete. Artifacts:"
echo "  $BUILD_DIR/src/libsysprop.a"
echo "  $BUILD_DIR/tools/sysprop"
echo "  $BUILD_DIR/tools/sysprop-init"
