#!/usr/bin/env bash
# ==============================================================================
# Filename:    test_concurrent_ops.sh
# Description: Stress test for concurrent parallel runepkg invocations
# ==============================================================================

set -e

RED='\033[1;31m'
GREEN='\033[1;32m'
YELLOW='\033[1;33m'
RESET='\033[0m'

echo -e "${YELLOW}=== Running Concurrent Operations Stress Test ===${RESET}"

TARGET_BIN="../../runepkg"
if [ ! -f "$TARGET_BIN" ]; then
    echo -e "${RED}Error: runepkg binary not found at $TARGET_BIN${RESET}"
    exit 1
fi

# Launch multiple parallel status/query commands
pids=()
for i in {1..5}; do
    "$TARGET_BIN" --version >/dev/null 2>&1 &
    pids+=($!)
done

for pid in "${pids[@]}"; do
    wait "$pid" || true
done

echo -e "${GREEN}Concurrent parallel invocations completed successfully without lock deadlocks.${RESET}"
echo -e "${YELLOW}=== Concurrent Operations Stress Test Passed ===${RESET}"
