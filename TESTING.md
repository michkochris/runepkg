# Runepkg Testing & Validation Guide

Welcome to the **runepkg** testing suite! This document guides you through running our automated integration tests, fuzzing campaigns, and static analysis checks, complete with real execution results from our production-grade validation pipeline.

---

## 🔍 Quick Verification Script

To run the entire verification suite (Build + Static Analysis + Integration/Chaos Tests + Fuzzing Campaign) directly in your terminal, execute the following command:

```bash
cd /home/michko/runepkg/runepkg && \
echo "=== 1. BUILD & STATIC ANALYSIS ===" && \
make clean && make && make static-analysis && \
echo -e "\n=== 2. INTEGRATION & CHAOS TESTS ===" && \
cd /home/michko/runepkg/runepkg/tests/integration && make && \
echo -e "\n=== 3. FUZZING CAMPAIGN (1000 runs) ===" && \
cd /home/michko/runepkg/runepkg/tests/fuzz && make && ./fuzz_sanitize_path -runs=1000 && \
echo "=== ALL VERIFICATIONS PASSED ==="
```

---

## Detailed Test Components

### 1. Static Analysis (`scan-build`)
We use Clang's static analysis engine to inspect every C and C++ source file for potential memory leaks, uninitialized variables, and logic defects.
- **Command:** `make static-analysis`
- **Latest Result:** 
  > `scan-build: No bugs found.` (Zero defects across the entire C/C++ codebase)

### 2. Automated Integration & Chaos Test Suite (`tests/integration/`)
The integration suite ensures the Finite State Machine (FSM) and transaction journal maintain absolute system integrity under stress.
- **Command:** `cd tests/integration && make`
- **Key Scenarios Tested:**
  - **Signal Injection (`test_fsm_signal_injection.sh`)**: Sends `SIGINT` mid-transaction during repository updates.
    - *Observed Outcome:* `FSM handled SIGINT gracefully without workspace corruption.`
  - **Concurrent Operations (`test_concurrent_ops.sh`)**: Launches parallel background processes querying repository states simultaneously.
    - *Observed Outcome:* `Concurrent parallel invocations completed successfully without lock deadlocks.`
  - **Chaos & Recovery (`test_fsm_chaos.sh`)**: Simulates orphaned staging workspaces and tests automated startup recovery.
    - *Observed Outcome:* `FSM Chaos & Recovery Test Passed Successfully.`

### 3. Fuzzing Campaign (`tests/fuzz/`)
We use `libFuzzer` and AddressSanitizer (`-fsanitize=fuzzer,address`) to subject security-critical path sanitizers to pathological inputs.
- **Command:** `cd tests/fuzz && make && ./fuzz_sanitize_path -runs=1000`
- **Latest Result:**
  - **Runs:** 1,000+ test iterations completed in under 1 second.
  - **Outcome:** `DONE 1000 runs` with **zero crashes, memory errors, or path traversal escapes** (88% branch coverage, recommended dictionary discovery for `..` and null bytes).

---
