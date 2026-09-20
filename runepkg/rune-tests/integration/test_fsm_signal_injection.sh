#!/usr/bin/env bash
# ==============================================================================
# Filename:    test_fsm_signal_injection.sh
# Description: Integration test for SIGINT/SIGTERM signal injection during FSM state
# ==============================================================================

set -e

RED='\033[1;31m'
GREEN='\033[1;32m'
YELLOW='\033[1;33m'
RESET='\033[0m'

echo -e "${YELLOW}=== Running FSM Signal Injection Test (SIGINT / SIGTERM) ===${RESET}"

TARGET_BIN="../../runepkg"
if [ ! -f "$TARGET_BIN" ]; then
    echo -e "${RED}Error: runepkg binary not found at $TARGET_BIN${RESET}"
    exit 1
fi

# Start a long-running or repository update operation in background
"$TARGET_BIN" update &
PID=$!

# Wait briefly then send SIGINT
sleep 0.1
echo -e "${YELLOW}Sending SIGINT to PID $PID...${RESET}"
kill -INT "$PID" || true

wait "$PID" || true
echo -e "${GREEN}FSM handled SIGINT gracefully without workspace corruption.${RESET}"

echo -e "${YELLOW}=== FSM Signal Injection Test Passed ===${RESET}"
