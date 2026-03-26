#!/usr/bin/env bash
# test.sh — Build and run the sysprop test suite.
#
# Usage:
#   ./scripts/test.sh [ctest-extra-args...]
#
# Examples:
#   ./scripts/test.sh
#   ./scripts/test.sh --rerun-failed
#   ./scripts/test.sh -R FileBackend       # run only tests matching a pattern

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$REPO_ROOT/build"

cmake -B "$BUILD_DIR" \
    -DSYSPROP_BUILD_TESTS=ON \
    "$REPO_ROOT"

cmake --build "$BUILD_DIR" --parallel "$(nproc 2>/dev/null || sysctl -n hw.logicalcpu)"

echo ""
ctest --test-dir "$BUILD_DIR" --output-on-failure "$@"
