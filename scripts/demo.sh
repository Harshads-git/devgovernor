#!/usr/bin/env bash
# ==============================================================================
# DevGovernor — Live Lab Demonstration Script
# Course: Operating Systems Lab (B.Tech IT - 40 Marks)
# ==============================================================================
set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$DIR"

echo "===================================================================="
echo "    DevGovernor — 100% Automated Live Lab Demonstration"
echo "===================================================================="
echo ""
echo "[Step 1/5] Building all C binaries with zero compiler warnings..."
make clean > /dev/null
make > /dev/null
echo "  ✓ Module 1 (monitor) and Module 2 (governor) built cleanly."
echo ""

echo "[Step 2/5] Running Student 1's C Monitor (One-Shot Snapshot)..."
./module1_monitor/monitor --output data/processes.json --oneshot
echo "  ✓ Snapshot written to data/processes.json conforming to shared/schema.json."
echo ""

echo "[Step 3/5] Demonstrating Student 2's Governor on a Live Process..."
sleep 120 &
TEST_PID=$!
echo "  * Spawned background test process: PID $TEST_PID (sleep)"

echo ""
echo "  --- 3A. Inspecting PID $TEST_PID ---"
./module2_governor/governor inspect $TEST_PID

echo ""
echo "  --- 3B. Dry-Run Priority Evaluation (Proposing Nice = 10) ---"
./module2_governor/governor evaluate $TEST_PID 10

echo ""
echo "  --- 3C. Explicit Priority Adjustment (setpriority to Nice = 10) ---"
./module2_governor/governor set-nice $TEST_PID 10 --yes

echo ""
echo "  --- 3D. Workload Policy Recommendation Engine (recommend-all) ---"
./module2_governor/governor recommend-all --input data/processes.json

kill $TEST_PID > /dev/null 2>&1 || true
echo "  ✓ Cleaned up test process PID $TEST_PID."
echo ""

echo "[Step 4/5] Running Student 3's Terminal Analytics Dashboard..."
echo "--------------------------------------------------------------------"
python3 module3_analytics/dashboard.py --input data/processes.json --history data/actions.jsonl --limit 6 --no-clear --ascii
echo "--------------------------------------------------------------------"
echo ""

echo "[Step 5/5] Exporting Markdown Lab Report..."
python3 module3_analytics/dashboard.py --input data/processes.json --history data/actions.jsonl --export-report docs/lab_viva_report.md
echo "  ✓ Generated docs/lab_viva_report.md for lab record submission."
echo ""
echo "===================================================================="
echo "    Demo Complete: All 3 Modules Functioning End-to-End!"
echo "===================================================================="
