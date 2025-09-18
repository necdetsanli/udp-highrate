#!/usr/bin/env bash
set -euo pipefail
# Helper if FetchContent internet is blocked. Requires libgtest-dev installed.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build-sysgtest"
rm -rf "$BUILD"
mkdir -p "$BUILD"
pushd "$BUILD" >/dev/null
cmake -DBUILD_TESTING=ON -DENABLE_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug -DGTEST_ROOT=/usr/src/googletest "$ROOT"
make -j
ctest --output-on-failure
popd >/dev/null