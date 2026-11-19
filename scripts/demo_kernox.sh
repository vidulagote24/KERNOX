#!/usr/bin/env bash
# ==============================================================================
# KERNOX 2.0: Android Resource Intelligence & Memory Reclamation Platform
# Automated End-to-End Demonstration Script
# ==============================================================================
# This script executes a reproducible, non-invasive demonstration of all
# retained KERNOX 2.0 capabilities across the core platform:
#
# Workflows Demonstrated:
#   1. System Status & Real Linux /proc Host Telemetry
#   2. Process Intelligence & Importance-Aware Memory Reclamation Run
#   3. Deterministic Workload Comparison Across All 4 Policies (FIFO, LRU, Priority, ImportanceAware)
#   4. Read-Only Android ADB Subsystem (Safe observation & offline fallback)
#   5. Python Analytics Pipeline (Dataset Validation, Publication Visualizations, & Multi-Scenario Reports)
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

cd "${PROJECT_ROOT}"

# Colors for terminal output
BOLD="\033[1m"
GREEN="\033[0;32m"
BLUE="\033[0;34m"
YELLOW="\033[0;33m"
CYAN="\033[0;36m"
RED="\033[0;31m"
RESET="\033[0m"

log_section() {
    echo -e "\n${BOLD}${BLUE}================================================================================${RESET}"
    echo -e "${BOLD}${CYAN}  $1${RESET}"
    echo -e "${BOLD}${BLUE}================================================================================${RESET}"
}

log_step() {
    echo -e "\n${BOLD}${GREEN}>> [STEP $1] $2${RESET}"
}

log_info() {
    echo -e "   ${YELLOW}*${RESET} $1"
}

# ------------------------------------------------------------------------------
# Environment Setup & Verification
# ------------------------------------------------------------------------------
log_section "KERNOX 2.0: SYSTEM VERIFICATION & SETUP"

BIN_PATH=""
if [[ -f "${PROJECT_ROOT}/build/release/bin/kernox" ]]; then
    BIN_PATH="${PROJECT_ROOT}/build/release/bin/kernox"
elif [[ -f "${PROJECT_ROOT}/build/debug/bin/kernox" ]]; then
    BIN_PATH="${PROJECT_ROOT}/build/debug/bin/kernox"
else
    echo -e "${RED}Error: KERNOX binary not found. Please build the project first:${RESET}"
    echo "  cmake --preset dev-release && ninja -C build/release"
    exit 1
fi

echo -e "KERNOX Binary : ${GREEN}${BIN_PATH}${RESET}"

# Locate Python environment
PYTHON_BIN=""
if [[ -f "${PROJECT_ROOT}/.venv/bin/python3" ]]; then
    PYTHON_BIN="${PROJECT_ROOT}/.venv/bin/python3"
elif [[ -f "${PROJECT_ROOT}/.venv/bin/python" ]]; then
    PYTHON_BIN="${PROJECT_ROOT}/.venv/bin/python"
elif command -v python3 >/dev/null 2>&1; then
    PYTHON_BIN="$(command -v python3)"
else
    echo -e "${RED}Error: python3 not found.${RESET}"
    exit 1
fi
echo -e "Python Binary : ${GREEN}${PYTHON_BIN}${RESET}"

# Setup designated demo output directory
DEMO_OUTPUT="${PROJECT_ROOT}/build/demo_output"
rm -rf "${DEMO_OUTPUT}"
mkdir -p "${DEMO_OUTPUT}/analytics"

echo -e "Demo Output   : ${GREEN}${DEMO_OUTPUT}${RESET}"

# ------------------------------------------------------------------------------
# 1. System Status & Host Telemetry
# ------------------------------------------------------------------------------
log_step "1/5" "Inspecting System Status & Real Linux /proc Telemetry"
"${BIN_PATH}" status

# ------------------------------------------------------------------------------
# 2. Process Intelligence & Single Policy Reclamation Run
# ------------------------------------------------------------------------------
log_step "2/5" "Demonstrating Process Intelligence & Importance-Aware Reclamation"
log_info "Executing High-Pressure scenario under Importance-Aware policy..."
"${BIN_PATH}" experiment run \
    --scenario high-pressure \
    --policy importance-aware \
    --capacity 1073741824 \
    --format text

# ------------------------------------------------------------------------------
# 3. Deterministic Workload Comparison Across All 4 Policies
# ------------------------------------------------------------------------------
log_step "3/5" "Running Multi-Policy Benchmark Comparison (FIFO vs LRU vs Priority vs ImportanceAware)"
log_info "Generating comparative benchmark for High-Pressure scenario (JSON export)..."
"${BIN_PATH}" experiment compare \
    --scenario high-pressure \
    --seed 42 \
    --capacity 1073741824 \
    --format json \
    --output "${DEMO_OUTPUT}/high_pressure_comparison.json"

log_info "Generating CSV comparison export for tabular review..."
"${BIN_PATH}" experiment compare \
    --scenario high-pressure \
    --seed 42 \
    --capacity 1073741824 \
    --format csv \
    --output "${DEMO_OUTPUT}/high_pressure_comparison.csv"

# ------------------------------------------------------------------------------
# 4. Read-Only Android ADB Subsystem
# ------------------------------------------------------------------------------
log_step "4/5" "Demonstrating Read-Only Android ADB Telemetry Subsystem"
log_info "Inspecting ADB binary availability and bridge status:"
"${BIN_PATH}" adb status || true

log_info "Discovering connected Android devices (handling offline / absent hardware safely):"
"${BIN_PATH}" adb devices || true

# ------------------------------------------------------------------------------
# 5. Python Analytics, Publication Visualizations & Reports
# ------------------------------------------------------------------------------
log_step "5/5" "Running Python Analytics & Publication Chart Generators"

export PYTHONPATH="${PROJECT_ROOT}/tools/analytics/src:${PYTHONPATH:-}"

log_info "A. Validating JSON & CSV dataset schemas and strict domain provenance:"
"${PYTHON_BIN}" -m kernox_analytics.cli validate \
    --input "${DEMO_OUTPUT}"

log_info "B. Analyzing multi-policy experiment comparison and generating figure suite:"
"${PYTHON_BIN}" -m kernox_analytics.cli analyze \
    --input "${DEMO_OUTPUT}/high_pressure_comparison.json" \
    --output "${DEMO_OUTPUT}/analytics"

log_info "C. Evaluating multi-scenario trade-offs across built-in workload scenarios:"
"${PYTHON_BIN}" -m kernox_analytics.cli compare \
    --input "${PROJECT_ROOT}/tools/analytics/examples/experiments" \
    --output "${DEMO_OUTPUT}/analytics"

# ------------------------------------------------------------------------------
# Summary of Generated Deliverables
# ------------------------------------------------------------------------------
echo -e "\n${BOLD}${GREEN}================================================================================${RESET}"
echo -e "${BOLD}${GREEN}  Inventory of Generated Demonstration Deliverables${RESET}"
echo -e "${BOLD}${GREEN}================================================================================${RESET}"

echo -e "\n${BOLD}Generated Experiment Datasets:${RESET}"
ls -lh "${DEMO_OUTPUT}"/*.json "${DEMO_OUTPUT}"/*.csv 2>/dev/null || true

echo -e "\n${BOLD}Generated Publication Visualizations:${RESET}"
ls -lh "${DEMO_OUTPUT}/analytics"/*.png 2>/dev/null || true

echo -e "\n${BOLD}Generated Markdown Reports:${RESET}"
ls -lh "${DEMO_OUTPUT}/analytics"/*.md 2>/dev/null || true

log_section "KERNOX 2.0: DEMONSTRATION COMPLETED SUCCESSFULLY"
echo -e "${GREEN}${BOLD}All retained subsystems verified and demonstrated operational.${RESET}"
echo -e "Inspect the generated reports and charts in: ${CYAN}${DEMO_OUTPUT}/analytics${RESET}\n"
