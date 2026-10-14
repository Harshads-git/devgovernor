# DevGovernor Testing & Performance Evaluation Guide

This document describes the automated test suite, manual verification procedures, and a reproducible performance-evaluation experiment for the DevGovernor project.

---

## 1. Automated Test Suite (`tests/test_all.py`)

The automated test suite runs using Python's built-in `unittest` framework and verifies all three modules without requiring third-party libraries.

### Running Automated Tests

```bash
# Build the C binaries first
make

# Run the full integration test suite (requires C binaries)
make test
# OR directly:
python3 tests/test_all.py

# Run Module 3 Dashboard standalone unit test suite (25 tests, pure Python):
python3 tests/test_dashboard.py
```

### Automated Test Coverage

| Test Case / Suite | Module | Description | Expected Outcome |
| :--- | :--- | :--- | :--- |
| `test_01_monitor_oneshot_json_validity` | Module 1 | Executes monitor with `--oneshot`, parses generated JSON, and validates types, ranges, and schema keys. | Valid JSON returned; fields conform to `shared/schema.json`. |
| `test_02_governor_safety_guard_pid_1` | Module 2 | Attempts to evaluate and change priority of PID 1 (`init`/`systemd`). | Rejected with non-zero exit code; audit logged with `rejected`. |
| `test_03_governor_nice_value_boundaries` | Module 2 | Evaluates out-of-bounds nice values (`-25`, `20`, `100`). | Blocked with error message; audit logged with `rejected`. |
| `test_04_governor_nonexistent_pid` | Module 2 | Runs inspect/evaluate on a non-existent PID (`999999`). | Handled gracefully with `ESRCH`/`ENOENT` message without crash. |
| `test_05_governor_dry_run_evaluation` | Module 2 | Runs inspect and evaluate on a spawned test process. | Recommendation logged without modifying live priority. |
| `test_06_dashboard_error_tolerance` | Module 3 | Feeds dashboard missing files, empty files, and malformed JSON. | Clean human-readable error messages displayed; zero crashes. |
| `tests/test_dashboard.py` (25 Tests) | Module 3 | Comprehensive suite testing zero-crash file ingestion, micro-retries, aggregation, sorting/filtering, ASCII/Unicode progress bars, governor live sync audit, and report generation. | 25/25 tests pass out-of-the-box with standard library Python. |

---

## 2. Manual Testing Guide

Follow these steps during your lab demonstration or viva to demonstrate end-to-end functionality.

### Step 1: Clean Build
```bash
make clean
make
```
*Verification:* Ensure both `module1_monitor/monitor` and `module2_governor/governor` compile without warnings.

### Step 2: One-Shot Process Monitor
```bash
./module1_monitor/monitor --output data/processes.json --oneshot
```
*Verification:* Inspect `data/processes.json`. Verify that it contains valid JSON with live PIDs, RSS memory in MB, and heuristic categories.

### Step 3: Continuous Monitoring
In Terminal 1:
```bash
./module1_monitor/monitor --output data/processes.json --interval 1
```

### Step 4: Launch Terminal Dashboard
In Terminal 2:
```bash
python3 module3_analytics/dashboard.py --input data/processes.json --history data/actions.jsonl --watch
```
*Verification:* Observe the table refreshing every 2 seconds. Press `Ctrl+C` when done.

### Step 5: Test Governor Commands
In Terminal 3, inspect a user process (e.g. your shell PID `$$` or a test process):
```bash
# Spawn a background test process
sleep 120 &
TEST_PID=$!

# 1. Inspect
./module2_governor/governor inspect $TEST_PID

# 2. Dry-Run Evaluate
./module2_governor/governor evaluate $TEST_PID 10

# 3. Apply Nice Adjustment (Standard user can raise nice value)
./module2_governor/governor set-nice $TEST_PID 10

# 4. Verify in Dashboard / Inspect
./module2_governor/governor inspect $TEST_PID

# Clean up
kill $TEST_PID
```

---

## 3. Reproducible Performance Evaluation Plan

### Objective
Demonstrate how adjusting Linux nice values affects the CPU time allocation between competing processes under the Completely Fair Scheduler (CFS).

### Experimental Setup
- **Workload:** Two identical CPU-bound mathematical loops running concurrently on a constrained number of CPU cores (pinned via `taskset -c 0` to a single core to prevent multi-core balancing from obscuring scheduler differences).
- **Control Group (Task A):** Runs with standard baseline priority (`nice = 0`).
- **Treatment Group (Task B):** Runs with throttled background priority (`nice = 10` or `nice = 15`).

### Execution Script (`tests/benchmark_priority.py`)

Create or run this test workload:
```python
import time, sys

def cpu_workload(iterations=40_000_000):
    total = 0
    start = time.time()
    for i in range(iterations):
        total += (i % 7) * (i % 3)
    elapsed = time.time() - start
    print(f"PID {sys.argv[1] if len(sys.argv) > 1 else 'N/A'} finished in {elapsed:.2f}s")

if __name__ == "__main__":
    cpu_workload()
```

### Running the Comparison:
```bash
# Terminal 1: Run Task A (Standard Priority, Nice 0) pinned to CPU 0
taskset -c 0 python3 -c "import time; t=time.time(); [i**2 for i in range(30000000)]; print('Task A done:', round(time.time()-t, 2), 's')" &
PID_A=$!

# Terminal 2: Run Task B (Throttled, Nice 10) pinned to the same CPU 0
taskset -c 0 python3 -c "import time; t=time.time(); [i**2 for i in range(30000000)]; print('Task B done:', round(time.time()-t, 2), 's')" &
PID_B=$!

# Adjust Priority using DevGovernor
./module2_governor/governor set-nice $PID_B 10

wait $PID_A $PID_B
```

### Expected Results & Observations
1. **Completion Time:** Task A (`nice = 0`) will complete significantly faster than Task B (`nice = 10`). Under CFS on a single shared core, nice 0 receives approximately $3\times$ to $4\times$ more CPU timeslices than nice 10.
2. **CPU Percentage:** While both tasks run concurrently, the monitor will observe Task A utilizing roughly 75% to 80% of the single core, while Task B receives the remaining 20% to 25%.

### Experimental Limitations & Honest Analysis
When presenting this in a viva, explain these real-world nuances:
1. **I/O-Bound Workloads:** Nice values only influence processes competing for CPU compute time. If a process is blocked on disk I/O, database queries, or network sockets (state `S` or `D`), changing its nice value will have zero observable impact.
2. **Uncontended Multi-Core Systems:** On modern 8-core / 16-thread processors (like the Ryzen 7 6800HS), if the system has idle CPU cores, CFS will simply run both tasks simultaneously on different physical cores at 100% CPU without any scheduling contention. Nice values only manifest prioritization when CPU demand exceeds available CPU cores.
3. **Developer Productivity Realism:** Lowering the priority of background indexing (e.g. language servers or asset compilation) reduces CPU contention and thermal throttling, keeping foreground interactive applications (like VS Code or terminals) snappy and responsive.
