#!/usr/bin/env bash
# benchmark.sh — Build and run the sysprop benchmarks.
#
# Usage:
#   ./scripts/benchmark.sh [benchmark-extra-args...]
#
# Examples:
#   ./scripts/benchmark.sh
#   ./scripts/benchmark.sh --benchmark_filter=BM_Get
#   ./scripts/benchmark.sh --benchmark_format=json --benchmark_out=results.json

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$REPO_ROOT/build"

cmake -B "$BUILD_DIR" \
    -DSYSPROP_BUILD_BENCHMARKS=ON \
    "$REPO_ROOT"

cmake --build "$BUILD_DIR" --parallel "$(nproc 2>/dev/null || sysctl -n hw.logicalcpu)"

echo ""
"$BUILD_DIR/benchmarks/sysprop_benchmark" --benchmark_counters_tabular=true "$@"
