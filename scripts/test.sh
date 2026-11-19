#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

cd "${PROJECT_ROOT}"

echo "========================================================="
echo "  KERNOX: Building and Running Automated Verification"
echo "========================================================="

PRESET="${1:-dev-debug}"

echo ">> Configuring CMake with preset: ${PRESET}"
cmake --preset "${PRESET}"

echo ">> Compiling all targets"
cmake --build --preset "${PRESET}"

echo ">> Running CTest test suite"
ctest --preset "${PRESET}" --output-on-failure

echo ">> Executing CLI smoke tests"
BIN_PATH="${PROJECT_ROOT}/build/debug/bin/kernox"
if [[ "${PRESET}" == *"release"* ]]; then
    BIN_PATH="${PROJECT_ROOT}/build/release/bin/kernox"
elif [[ "${PRESET}" == *"asan"* ]]; then
    BIN_PATH="${PROJECT_ROOT}/build/asan/bin/kernox"
elif [[ "${PRESET}" == *"tsan"* ]]; then
    BIN_PATH="${PROJECT_ROOT}/build/tsan/bin/kernox"
fi

echo ">> Running ${BIN_PATH} --version"
"${BIN_PATH}" --version

echo ">> Running ${BIN_PATH} status"
"${BIN_PATH}" status

echo ">> Running ${BIN_PATH} self-test"
"${BIN_PATH}" self-test

echo ">> Running ${BIN_PATH} experiment run --scenario normal --policy importance-aware"
"${BIN_PATH}" experiment run --scenario normal --policy importance-aware

echo ">> Running ${BIN_PATH} experiment compare --scenario normal"
"${BIN_PATH}" experiment compare --scenario normal

echo ">> Running ${BIN_PATH} adb status"
"${BIN_PATH}" adb status || true

echo ">> Running ${BIN_PATH} adb devices"
"${BIN_PATH}" adb devices || true

echo ">> Running ${BIN_PATH} simulate --help"
"${BIN_PATH}" simulate --help

echo "========================================================="
echo "  All verification targets executed successfully."
echo "========================================================="
