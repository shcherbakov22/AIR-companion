#!/usr/bin/env bash
# run-coverage.sh — build with coverage, run tests, generate lcov HTML report.
#
# Requires: cmake, gcc or clang with --coverage support, lcov, genhtml.
#
# Usage:
#   ./tests/run-coverage.sh          # build + run + report (default build/linux-coverage)
#   ./tests/run-coverage.sh clean   # wipe build/linux-coverage first

set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build/linux-coverage}"
COV_DIR="${BUILD_DIR}/coverage"
TEST_BIN="${BUILD_DIR}/tests/air_companion_tests"

if [[ "${1:-}" == "clean" ]]; then
    rm -rf "$BUILD_DIR"
fi

echo "=== Configuring (coverage build) ==="
cmake --preset linux-coverage
cmake --build --preset linux-coverage

echo "=== Running tests ==="
"$TEST_BIN"

echo "=== Generating coverage report ==="
mkdir -p "$COV_DIR"
pushd "$COV_DIR" > /dev/null
lcov --directory "$BUILD_DIR" --capture --output-file lcov.info --no-external 2>/dev/null || true
genhtml lcov.info --output-directory html --title "AIR Companion Coverage" 2>/dev/null || true
popd > /dev/null

echo "Coverage report: ${COV_DIR}/html/index.html"
