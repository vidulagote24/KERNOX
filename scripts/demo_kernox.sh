#!/usr/bin/env bash
# ==============================================================================
# KERNOX 2.0: Android & Linux Resource Intelligence Platform
# Automated End-to-End Demonstration Script
# ==============================================================================
# This script executes a reproducible, non-invasive demonstration of all
# KERNOX 2.0 capabilities across the complete 7-phase architecture.
#
# Workflows Demonstrated:
#   1. System Telemetry & Foundation Verification
#   2. Classic OS Simulation (CPU scheduling, Memory paging, IPC/Sync)
#   3. Process Intelligence & Android Importance Tiers
#   4. Memory Pressure Detection (75% Moderate, 90% Critical)
#   5. Policy-Based Memory Reclamation (FIFO, LRU, Priority, ImportanceAware)
#   6. Deterministic Workload Generation & Benchmark Comparison (JSON/CSV)
#   7. Read-Only Android ADB Subsystem (Graceful handling of absent device)
#   8. Python Analytics Pipeline, Publication Visualizations, & Markdown Reports
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
# 1. Locate Executables & Environment Verification
# ------------------------------------------------------------------------------
log_section "KERNOX 2.0: SYSTEM VERIFICATION & SETUP"

BIN_PATH=""
if [[ -f "${PROJECT_ROOT}/build/release/bin/kernox" ]]; then
    BIN_PATH="${PROJECT_ROOT}/build/release/bin/kernox"
elif [[ -f "${PROJECT_ROOT}/build/debug/bin/kernox" ]]; then
    BIN_PATH="${PROJECT_ROOT}/build/debug/bin/kernox"
elif [[ -f "${PROJECT_ROOT}/build/asan/bin/kernox" ]]; then
    BIN_PATH="${PROJECT_ROOT}/build/asan/bin/kernox"
else
    echo -e "${RED}Error: KERNOX binary not found. Please build the project first:${RESET}"
    echo "  cmake --preset dev-debug && cmake --build --preset dev-debug"
    exit 1
fi

echo -e "KERNOX Binary : ${GREEN}${BIN_PATH}${RESET}"

# Locate Python environment
PYTHON_BIN=""
if [[ -f "${PROJECT_ROOT}/tools/analytics/.venv/bin/python3" ]]; then
    PYTHON_BIN="${PROJECT_ROOT}/tools/analytics/.venv/bin/python3"
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
# 2. System Status & Host Telemetry
# ------------------------------------------------------------------------------
log_step "1/7" "Inspecting System Status & Real Linux /proc Telemetry"
"${BIN_PATH}" status

# ------------------------------------------------------------------------------
# 3. Classic OS Simulation
# ------------------------------------------------------------------------------
log_step "2/7" "Executing Classic OS Simulation Modules"

log_info "A. CPU Scheduling (Shortest Remaining Time First with dynamic arrivals):"
"${BIN_PATH}" schedule --algorithm srtf --workload arrivals

log_info "B. Virtual Memory Page Replacement (Least Recently Used with 3 frames):"
"${BIN_PATH}" memory replace --algorithm lru --workload page-reference-demo --frames 3

log_info "C. Cross-Process Synchronization (Atomic POSIX Ring Buffer Demo):"
"${BIN_PATH}" sync demo

# ------------------------------------------------------------------------------
# 4. Process Intelligence & Single Policy Reclamation Run
# ------------------------------------------------------------------------------
log_step "3/7" "Demonstrating Process Intelligence & Importance-Aware Reclamation"
log_info "Executing HighMemoryPressure scenario under ImportanceAware policy..."
"${BIN_PATH}" experiment run \
    --scenario high-pressure \
    --policy importance-aware \
    --capacity 1073741824 \
    --format text

# ------------------------------------------------------------------------------
# 5. Deterministic Workload Comparison Across All 4 Policies
# ------------------------------------------------------------------------------
log_step "4/7" "Running Multi-Policy Benchmark Comparison (FIFO vs LRU vs Priority vs ImportanceAware)"
log_info "Generating comparative benchmark for HighMemoryPressure scenario..."
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
# 6. Read-Only Android ADB Subsystem
# ------------------------------------------------------------------------------
log_step "5/7" "Demonstrating Read-Only Android ADB Telemetry Subsystem"
log_info "Inspecting ADB binary availability and bridge status:"
"${BIN_PATH}" adb status || true

log_info "Discovering connected Android devices (handling offline / absent hardware safely):"
if ! "${BIN_PATH}" adb devices 2>/dev/null; then
    echo -e "   ${YELLOW}[NOTICE] Physical Android hardware is absent on this host.${RESET}"
    echo -e "   ${YELLOW}KERNOX strictly isolates physical hardware from simulation.${RESET}"
    echo -e "   ${YELLOW}Demonstrating ADB telemetry ingestion using validated emulator snapshot fixture.${RESET}"
fi

# ------------------------------------------------------------------------------
# 7. Python Analytics, Publication Visualizations & Reports
# ------------------------------------------------------------------------------
log_step "6/7" "Running Python Analytics & Publication Chart Generators"

export PYTHONPATH="${PROJECT_ROOT}/tools/analytics/src:${PYTHONPATH:-}"

log_info "A. Validating JSON & CSV dataset schemas and strict domain provenance:"
"${PYTHON_BIN}" -m kernox_analytics.cli validate \
    --input "${DEMO_OUTPUT}"

log_info "B. Analyzing multi-policy experiment comparison and generating figure suite:"
"${PYTHON_BIN}" -m kernox_analytics.cli analyze \
    --input "${DEMO_OUTPUT}/high_pressure_comparison.json" \
    --output "${DEMO_OUTPUT}/analytics"

log_info "C. Evaluating multi-scenario trade-offs across all 5 workload scenarios:"
"${PYTHON_BIN}" -m kernox_analytics.cli compare \
    --input "${PROJECT_ROOT}/tools/analytics/examples/experiments" \
    --output "${DEMO_OUTPUT}/analytics"

log_info "D. Processing Android observation snapshot and non-additive memory analytics:"
"${PYTHON_BIN}" -m kernox_analytics.cli android \
    --input "${PROJECT_ROOT}/tools/analytics/examples/android/pixel7_snapshot.json" \
    --output "${DEMO_OUTPUT}/analytics"

# ------------------------------------------------------------------------------
# 8. Summary of Generated Deliverables
# ------------------------------------------------------------------------------
log_step "7/7" "Inventory of Generated Demonstration Deliverables"

echo -e "\n${BOLD}Generated Experiment Datasets:${RESET}"
ls -lh "${DEMO_OUTPUT}"/*.json "${DEMO_OUTPUT}"/*.csv 2>/dev/null || true

echo -e "\n${BOLD}Generated Publication Visualizations:${RESET}"
ls -lh "${DEMO_OUTPUT}/analytics"/*.png "${DEMO_OUTPUT}/analytics/android"/*.png 2>/dev/null || true

echo -e "\n${BOLD}Generated Markdown Reports:${RESET}"
ls -lh "${DEMO_OUTPUT}/analytics"/*.md 2>/dev/null || true

log_section "KERNOX 2.0: DEMONSTRATION COMPLETED SUCCESSFULLY"
echo -e "${GREEN}${BOLD}All 7 project phases verified and demonstrated operational.${RESET}"
echo -e "Inspect the generated reports and charts in: ${CYAN}${DEMO_OUTPUT}/analytics${RESET}\n"
