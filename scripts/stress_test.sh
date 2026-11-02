#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

cd "${PROJECT_ROOT}"

echo "========================================================="
echo "  KERNOX: 5-Iteration Stress Verification"
echo "========================================================="

BIN_DIR="${PROJECT_ROOT}/build/release/bin"

SUITES=("test_sync" "test_unix_socket" "test_message_queue" "test_telemetry")

for suite in "${SUITES[@]}"; do
    echo ">> Running 5 stress iterations of ${suite}:"
    for i in 1 2 3 4 5; do
        printf "   Iteration %d/5: " "${i}"
        "${BIN_DIR}/${suite}" > /dev/null
        echo "PASS"
    done
done

echo "========================================================="
echo "  KERNOX: All 5 Stress Iterations Passed With Zero Flakes!"
echo "========================================================="
