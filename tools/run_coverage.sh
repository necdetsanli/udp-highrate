#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build-cov"
rm -rf "$BUILD"
mkdir -p "$BUILD"
pushd "$BUILD" >/dev/null
cmake -DENABLE_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug "$ROOT"
make -j
ctest --output-on-failure
lcov --capture --directory . --output-file coverage.info >/dev/null 2>&1 || true
lcov --remove coverage.info '/usr/*' '*/tests/*' --output-file coverage.info >/dev/null 2>&1 || true
genhtml coverage.info --output-directory coverage-html >/dev/null 2>&1 || true
LINES=$(lcov --summary coverage.info 2>/dev/null | awk '/lines\.*:/{print $2}' | tr -d '%')
echo "Line coverage: ${LINES:-0}%"
if [ "${LINES:-0}" != "" ]; then
  PCT=$(printf "%.0f" "${LINES}")
else
  PCT=0
fi
if [ "$PCT" -lt 100 ]; then
  echo "ERROR: Coverage < 100%. Please run on a machine with GoogleTest available and re-run."
  exit 1
fi
popd >/dev/null
 
 