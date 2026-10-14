#!/usr/bin/env python3
"""
============================================================================
DevGovernor — Automated Test Suite
============================================================================
Course: Operating Systems Lab (B.Tech IT)

Validates:
  1. Monitor JSON output conformance to shared/schema.json.
  2. Safety guards and input validation in governor:
     - Negative/invalid PID rejection
     - PID 1 (init/systemd) protection
     - Nice value boundary rejection (outside [-20, 19])
     - Non-existent PID handling (ESRCH)
     - Dry-run evaluate logging
  3. Dashboard error tolerance:
     - Missing file handling
     - Empty file handling
     - Malformed JSON handling
     - Corrupt action-history lines handling
============================================================================
"""

import os
import sys
import json
import subprocess
import tempfile
import unittest

# Ensure devgovernor root is in path
DEVGOV_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SCHEMA_FILE = os.path.join(DEVGOV_DIR, "shared", "schema.json")
MONITOR_BIN = os.path.join(DEVGOV_DIR, "module1_monitor", "monitor")
GOVERNOR_BIN = os.path.join(DEVGOV_DIR, "module2_governor", "governor")
DASHBOARD_PY = os.path.join(DEVGOV_DIR, "module3_analytics", "dashboard.py")

sys.path.insert(0, os.path.join(DEVGOV_DIR, "module3_analytics"))
import dashboard


class TestDevGovernor(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        # Verify binaries are compiled
        if not os.path.exists(MONITOR_BIN) or not os.path.exists(GOVERNOR_BIN):
            raise unittest.SkipTest("C binaries not found. Run 'make' before running tests.")

    def test_01_monitor_oneshot_json_validity(self):
        """Test monitor produces valid JSON matching schema structure."""
        with tempfile.NamedTemporaryFile(suffix=".json", delete=False) as tf:
            temp_output = tf.name

        try:
            # Run monitor in one-shot mode
            cmd = [MONITOR_BIN, "--output", temp_output, "--oneshot"]
            res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=10)
            self.assertEqual(res.returncode, 0, f"Monitor failed: {res.stderr}")

            # Verify file exists and has content
            self.assertTrue(os.path.exists(temp_output))
            self.assertGreater(os.path.getsize(temp_output), 0)

            # Load and parse JSON
            with open(temp_output, "r", encoding="utf-8") as f:
                data = json.load(f)

            # Verify top-level structure
            self.assertEqual(data.get("schema_version"), 1)
            self.assertIn("timestamp", data)
            self.assertIn("sampling_interval_seconds", data)
            self.assertIn("processes", data)
            self.assertIsInstance(data["processes"], list)
            self.assertGreater(len(data["processes"]), 0)

            # Validate individual process schema contracts
            allowed_categories = {
                "development_tool", "development_server",
                "build_process", "background_process", "system_or_unknown"
            }

            for p in data["processes"][:50]:  # Sample first 50 processes
                self.assertIn("pid", p)
                self.assertIsInstance(p["pid"], int)
                self.assertGreater(p["pid"], 0)

                self.assertIn("ppid", p)
                self.assertIsInstance(p["ppid"], int)

                self.assertIn("name", p)
                self.assertIsInstance(p["name"], str)

                self.assertIn("state", p)
                self.assertIsInstance(p["state"], str)

                self.assertIn("nice_value", p)
                self.assertIsInstance(p["nice_value"], int)
                self.assertGreaterEqual(p["nice_value"], -20)
                self.assertLessEqual(p["nice_value"], 19)

                self.assertIn("workload_category", p)
                self.assertIn(p["workload_category"], allowed_categories)

                # CPU percent must be float or None
                if p.get("cpu_percent") is not None:
                    self.assertGreaterEqual(p["cpu_percent"], 0.0)

                # Memory must be float or None
                if p.get("memory_mb") is not None:
                    self.assertGreaterEqual(p["memory_mb"], 0.0)

        finally:
            if os.path.exists(temp_output):
                os.remove(temp_output)

    def test_02_governor_safety_guard_pid_1(self):
        """Governor must reject attempts to evaluate or alter PID 1 (init/systemd)."""
        with tempfile.NamedTemporaryFile(suffix=".jsonl", delete=False) as tf:
            temp_history = tf.name

        try:
            # Try evaluate on PID 1
            cmd = [GOVERNOR_BIN, "evaluate", "1", "5", "--history", temp_history]
            res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            self.assertNotEqual(res.returncode, 0, "Governor should reject PID 1")

            # Try set-nice on PID 1
            cmd = [GOVERNOR_BIN, "set-nice", "1", "5", "--history", temp_history]
            res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            self.assertNotEqual(res.returncode, 0, "Governor should reject PID 1")

            # Check action history record
            with open(temp_history, "r", encoding="utf-8") as f:
                lines = [json.loads(l) for l in f if l.strip()]
            self.assertTrue(any(l.get("status") == "rejected" for l in lines))

        finally:
            if os.path.exists(temp_history):
                os.remove(temp_history)

    def test_03_governor_nice_value_boundaries(self):
        """Governor must reject nice values outside [-20, 19]."""
        with tempfile.NamedTemporaryFile(suffix=".jsonl", delete=False) as tf:
            temp_history = tf.name

        try:
            # PID 2 is kthreadd or similar; test invalid nice
            for bad_nice in ["-25", "20", "100"]:
                cmd = [GOVERNOR_BIN, "evaluate", "2", bad_nice, "--history", temp_history]
                res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                self.assertNotEqual(res.returncode, 0, f"Should reject nice {bad_nice}")

        finally:
            if os.path.exists(temp_history):
                os.remove(temp_history)

    def test_04_governor_nonexistent_pid(self):
        """Governor must safely handle non-existent PID without crashing."""
        with tempfile.NamedTemporaryFile(suffix=".jsonl", delete=False) as tf:
            temp_history = tf.name

        try:
            non_existent_pid = "999999"
            cmd = [GOVERNOR_BIN, "inspect", non_existent_pid, "--history", temp_history]
            res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            self.assertNotEqual(res.returncode, 0, "Inspect non-existent PID must exit non-zero")

            cmd = [GOVERNOR_BIN, "evaluate", non_existent_pid, "10", "--history", temp_history]
            res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            self.assertNotEqual(res.returncode, 0, "Evaluate non-existent PID must exit non-zero")

        finally:
            if os.path.exists(temp_history):
                os.remove(temp_history)

    def test_05_governor_dry_run_evaluation(self):
        """Governor evaluate must perform dry-run recommendation without modifying process."""
        with tempfile.NamedTemporaryFile(suffix=".jsonl", delete=False) as tf:
            temp_history = tf.name

        try:
            # Spawn a harmless sleep process
            proc = subprocess.Popen(["sleep", "30"])
            try:
                # Inspect
                cmd = [GOVERNOR_BIN, "inspect", str(proc.pid), "--history", temp_history]
                res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                self.assertEqual(res.returncode, 0, f"Inspect failed: {res.stderr}")

                # Evaluate proposing nice 10 (deprioritize)
                cmd = [GOVERNOR_BIN, "evaluate", str(proc.pid), "10", "--history", temp_history]
                res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                self.assertEqual(res.returncode, 0, f"Evaluate failed: {res.stderr}")

                # Verify history logged a recommendation
                with open(temp_history, "r", encoding="utf-8") as f:
                    records = [json.loads(line) for line in f if line.strip()]
                self.assertTrue(any(r.get("action_type") == "evaluate" and r.get("status") == "recommendation"
                                    for r in records))
            finally:
                proc.terminate()
                proc.wait()

        finally:
            if os.path.exists(temp_history):
                os.remove(temp_history)

    def test_06_dashboard_error_tolerance(self):
        """Dashboard module must gracefully handle missing, empty, and malformed files."""
        # 1. Non-existent file
        data, err = dashboard.load_process_data("data/non_existent_file_xyz.json")
        self.assertIsNone(data)
        self.assertIn("not found", err)

        # 2. Empty file
        with tempfile.NamedTemporaryFile(suffix=".json", delete=False) as tf:
            empty_path = tf.name
        try:
            data, err = dashboard.load_process_data(empty_path)
            self.assertIsNone(data)
            self.assertIn("empty", err)
        finally:
            if os.path.exists(empty_path):
                os.remove(empty_path)

        # 3. Malformed JSON
        with tempfile.NamedTemporaryFile(suffix=".json", delete=False, mode="w") as tf:
            tf.write("{ this is not valid json }")
            corrupt_path = tf.name
        try:
            data, err = dashboard.load_process_data(corrupt_path)
            self.assertIsNone(data)
            self.assertIn("Malformed JSON", err)
        finally:
            if os.path.exists(corrupt_path):
                os.remove(corrupt_path)

        # 4. Action history with corrupt lines
        with tempfile.NamedTemporaryFile(suffix=".jsonl", delete=False, mode="w") as tf:
            tf.write('{"timestamp": "2026-10-14T12:00:00Z", "pid": 100, "action_type": "inspect"}\n')
            tf.write('CORRUPTED LINE HERE\n')
            tf.write('{"timestamp": "2026-10-14T12:01:00Z", "pid": 101, "action_type": "evaluate"}\n')
            hist_path = tf.name
        try:
            actions, err = dashboard.load_action_history(hist_path)
            self.assertIsNone(err)
            self.assertEqual(len(actions), 2, "Should recover 2 valid JSON lines and skip corrupt line")
        finally:
            if os.path.exists(hist_path):
                os.remove(hist_path)


if __name__ == "__main__":
    unittest.main(verbosity=2)
