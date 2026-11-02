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

echo ">> Running ${BIN_PATH} schedule --algorithm srtf --workload arrivals"
"${BIN_PATH}" schedule --algorithm srtf --workload arrivals

echo ">> Running ${BIN_PATH} disk schedule --algorithm sstf --workload canonical"
"${BIN_PATH}" disk schedule --algorithm sstf --workload canonical

echo ">> Running ${BIN_PATH} deadlock banker --workload safe"
"${BIN_PATH}" deadlock banker --workload safe

echo ">> Running ${BIN_PATH} deadlock detect --workload detection-demo"
"${BIN_PATH}" deadlock detect --workload detection-demo

echo ">> Running ${BIN_PATH} deadlock graph --workload cycle-demo"
"${BIN_PATH}" deadlock graph --workload cycle-demo

echo ">> Running ${BIN_PATH} deadlock request --workload safe --process 1 --request 1,0,2"
"${BIN_PATH}" deadlock request --workload safe --process 1 --request 1,0,2

echo ">> Running ${BIN_PATH} memory allocate --algorithm first-fit --workload allocation-demo"
"${BIN_PATH}" memory allocate --algorithm first-fit --workload allocation-demo

echo ">> Running ${BIN_PATH} memory paging --workload paging-demo"
"${BIN_PATH}" memory paging --workload paging-demo

echo ">> Running ${BIN_PATH} memory translate --process 0 --address 4096 --workload paging-demo"
"${BIN_PATH}" memory translate --process 0 --address 4096 --workload paging-demo

echo ">> Running ${BIN_PATH} memory replace --algorithm lru --workload page-reference-demo --frames 3"
"${BIN_PATH}" memory replace --algorithm lru --workload page-reference-demo --frames 3

echo ">> Running ${BIN_PATH} memory ws --workload page-reference-demo --window 4"
"${BIN_PATH}" memory ws --workload page-reference-demo --window 4

echo ">> Running ${BIN_PATH} process stat 1"
"${BIN_PATH}" process stat 1

echo ">> Running ${BIN_PATH} sync demo"
"${BIN_PATH}" sync demo

echo ">> Running ${BIN_PATH} ipc unix-demo"
"${BIN_PATH}" ipc unix-demo

echo ">> Running ${BIN_PATH} ipc mq-demo"
"${BIN_PATH}" ipc mq-demo

echo "========================================================="
echo "  KERNOX: All Verification Checks Passed Successfully!"
echo "========================================================="
