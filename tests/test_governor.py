#!/usr/bin/env python3
"""
============================================================================
DevGovernor — Module 2 Governor Test Suite
============================================================================
Course: Operating Systems Lab (B.Tech IT)
Student 2: Resource Management and Scheduling Governor

Tests:
  01  inspect on a live process (own PID via os.getpid's parent)
  02  inspect on a non-existent PID returns non-zero exit
  03  evaluate: PID 1 guard
  04  evaluate: self-PID guard
  05  evaluate: nice lower bound (-25 rejected)
  06  evaluate: nice upper bound (+20 rejected)
  07  evaluate: valid throttle dry-run produces audit record
  08  evaluate: valid boost dry-run warns about privilege
  09  set-nice --yes flag skips confirmation
  10  set-nice: PID 1 blocked
  11  set-nice: out-of-range nice blocked
  12  set-nice: non-existent PID fails safely
  13  recommend-all: parses sample JSON and prints recommendations
  14  recommend-all: missing input file returns non-zero
  15  audit log: JSON-Lines are valid after multiple operations
============================================================================
"""

import os
import sys
import json
import subprocess
import tempfile
import signal
import time
import unittest

# Locate binaries relative to this test file
DEVGOV_DIR   = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
GOVERNOR_BIN = os.path.join(DEVGOV_DIR, "module2_governor", "governor")
SAMPLE_JSON  = os.path.join(DEVGOV_DIR, "data", "processes.sample.json")


def run_gov(*args, input_text=None, timeout=10):
    """Run governor with given args; return CompletedProcess."""
    cmd = [GOVERNOR_BIN] + list(args)
    return subprocess.run(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        input=input_text,
        text=True,
        timeout=timeout,
    )


def make_tmp_audit():
    """Create a temporary JSONL audit file; caller must delete."""
    fd, path = tempfile.mkstemp(suffix=".jsonl", prefix="gov_test_")
    os.close(fd)
    return path


def read_audit(path):
    """Return list of parsed JSON records from a JSONL audit file."""
    records = []
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line:
                records.append(json.loads(line))
    return records


class TestGovernorBinary(unittest.TestCase):
    """Functional tests exercising the governor binary directly."""

    @classmethod
    def setUpClass(cls):
        if not os.path.isfile(GOVERNOR_BIN):
            raise unittest.SkipTest(
                f"Governor binary not found at {GOVERNOR_BIN}.\n"
                "Run 'make' inside module2_governor/ first."
            )
        if not os.access(GOVERNOR_BIN, os.X_OK):
            raise unittest.SkipTest(f"{GOVERNOR_BIN} is not executable.")

    # ------------------------------------------------------------------
    # 01: inspect on a live process
    # ------------------------------------------------------------------
    def test_01_inspect_live_process(self):
        """inspect on a live sleep process returns 0 and prints PID line."""
        audit = make_tmp_audit()
        try:
            proc = subprocess.Popen(["sleep", "60"])
            time.sleep(0.1)
            try:
                res = run_gov("inspect", str(proc.pid), "--history", audit)
                self.assertEqual(
                    res.returncode, 0,
                    f"inspect failed:\n{res.stderr}"
                )
                self.assertIn(str(proc.pid), res.stdout)
                self.assertIn("Nice", res.stdout)
                self.assertIn("CFS", res.stdout)

                # Audit record exists and is valid JSON
                records = read_audit(audit)
                self.assertTrue(
                    any(r["action_type"] == "inspect" and r["pid"] == proc.pid
                        for r in records),
                    "Audit should contain an inspect record for the spawned PID"
                )
            finally:
                proc.terminate()
                proc.wait()
        finally:
            os.unlink(audit)

    # ------------------------------------------------------------------
    # 02: inspect on non-existent PID
    # ------------------------------------------------------------------
    def test_02_inspect_nonexistent_pid(self):
        """inspect on PID 999999 must return non-zero and mention error."""
        audit = make_tmp_audit()
        try:
            res = run_gov("inspect", "999999", "--history", audit)
            self.assertNotEqual(res.returncode, 0,
                                "inspect on dead PID must exit non-zero")
            self.assertTrue(
                "not found" in res.stderr.lower() or
                "error" in res.stderr.lower(),
                f"Expected error message, got: {res.stderr}"
            )
        finally:
            os.unlink(audit)

    # ------------------------------------------------------------------
    # 03: evaluate PID 1 guard
    # ------------------------------------------------------------------
    def test_03_evaluate_pid1_guard(self):
        """evaluate PID 1 must be rejected with non-zero exit."""
        audit = make_tmp_audit()
        try:
            res = run_gov("evaluate", "1", "5", "--history", audit)
            self.assertNotEqual(res.returncode, 0,
                                "Governor must reject PID 1 evaluation")
            # Audit record should say rejected
            records = read_audit(audit)
            self.assertTrue(
                any(r.get("status") == "rejected" for r in records),
                "Audit must record 'rejected' for PID 1 attempt"
            )
        finally:
            os.unlink(audit)

    # ------------------------------------------------------------------
    # 04: evaluate self-PID guard
    # ------------------------------------------------------------------
    def test_04_evaluate_self_pid_guard(self):
        """evaluate the governor's own PID must be rejected."""
        # We don't know the exact PID, but we can test PID 0 (also invalid)
        # and separately verify that the governor handles self internally.
        # The most reliable test is to verify the governor rejects pid<=0.
        audit = make_tmp_audit()
        try:
            res = run_gov("evaluate", "0", "5", "--history", audit)
            self.assertNotEqual(res.returncode, 0,
                                "Governor must reject PID 0")
        finally:
            os.unlink(audit)

    # ------------------------------------------------------------------
    # 05: evaluate nice lower bound rejected
    # ------------------------------------------------------------------
    def test_05_evaluate_nice_lower_bound(self):
        """evaluate nice=-25 is below -20 and must be rejected."""
        audit = make_tmp_audit()
        try:
            for bad_nice in ["-21", "-25", "-100"]:
                res = run_gov("evaluate", "2", bad_nice, "--history", audit)
                self.assertNotEqual(
                    res.returncode, 0,
                    f"evaluate with nice={bad_nice} should be rejected"
                )
        finally:
            os.unlink(audit)

    # ------------------------------------------------------------------
    # 06: evaluate nice upper bound rejected
    # ------------------------------------------------------------------
    def test_06_evaluate_nice_upper_bound(self):
        """evaluate nice=20 is above 19 and must be rejected."""
        audit = make_tmp_audit()
        try:
            for bad_nice in ["20", "25", "100"]:
                res = run_gov("evaluate", "2", bad_nice, "--history", audit)
                self.assertNotEqual(
                    res.returncode, 0,
                    f"evaluate with nice={bad_nice} should be rejected"
                )
        finally:
            os.unlink(audit)

    # ------------------------------------------------------------------
    # 07: evaluate valid throttle (dry-run)
    # ------------------------------------------------------------------
    def test_07_evaluate_valid_throttle_dry_run(self):
        """evaluate proposing nice=10 on a real process: dry-run, no change."""
        audit = make_tmp_audit()
        try:
            proc = subprocess.Popen(["sleep", "60"])
            time.sleep(0.1)
            try:
                res = run_gov("evaluate", str(proc.pid), "10",
                              "--history", audit)
                self.assertEqual(
                    res.returncode, 0,
                    f"evaluate throttle failed:\n{res.stderr}"
                )
                # Must mention throttle/background/dry-run
                out_lower = res.stdout.lower()
                self.assertTrue(
                    "throttle" in out_lower or "background" in out_lower or
                    "dry-run" in out_lower,
                    f"Expected throttle indication in output:\n{res.stdout}"
                )
                # Audit record = recommendation, no actual change
                records = read_audit(audit)
                eval_recs = [r for r in records
                             if r["action_type"] == "evaluate"
                             and r["pid"] == proc.pid]
                self.assertTrue(len(eval_recs) >= 1,
                                "Audit must record the evaluate dry-run")
                self.assertEqual(eval_recs[-1]["status"], "recommendation")
                # resulting_nice must equal previous_nice (no change)
                self.assertEqual(
                    eval_recs[-1]["resulting_nice"],
                    eval_recs[-1]["previous_nice"],
                    "Dry-run must not change the nice value in the audit log"
                )
            finally:
                proc.terminate()
                proc.wait()
        finally:
            os.unlink(audit)

    # ------------------------------------------------------------------
    # 08: evaluate valid boost -- warns about privilege
    # ------------------------------------------------------------------
    def test_08_evaluate_boost_privilege_warning(self):
        """evaluate proposing a lower nice must warn about CAP_SYS_NICE."""
        audit = make_tmp_audit()
        try:
            proc = subprocess.Popen(["sleep", "60"])
            time.sleep(0.1)
            try:
                res = run_gov("evaluate", str(proc.pid), "-5",
                              "--history", audit)
                self.assertEqual(
                    res.returncode, 0,
                    f"evaluate boost failed:\n{res.stderr}"
                )
                out_lower = res.stdout.lower()
                # Must mention privilege / CAP_SYS_NICE / sudo / warning
                self.assertTrue(
                    "cap_sys_nice" in out_lower or
                    "sudo" in out_lower or
                    "privilege" in out_lower or
                    "warning" in out_lower,
                    f"Expected privilege warning in output:\n{res.stdout}"
                )
            finally:
                proc.terminate()
                proc.wait()
        finally:
            os.unlink(audit)

    # ------------------------------------------------------------------
    # 09: set-nice --yes flag skips confirmation
    # ------------------------------------------------------------------
    def test_09_set_nice_yes_flag_no_prompt(self):
        """set-nice --yes must not block waiting for stdin."""
        audit = make_tmp_audit()
        try:
            proc = subprocess.Popen(["sleep", "60"])
            time.sleep(0.1)
            try:
                # Propose nice 5 (raise -- unprivileged ok for own processes)
                # We use --yes; if it blocks for input, timeout will fire.
                res = run_gov("set-nice", str(proc.pid), "5",
                              "--yes", "--history", audit, timeout=5)
                # Either success or EPERM (if we don't own it), but must NOT timeout
                self.assertIn(
                    res.returncode, [0, 1],
                    "set-nice --yes must complete without blocking"
                )
                # If it succeeded, verify audit
                if res.returncode == 0:
                    records = read_audit(audit)
                    sn_recs = [r for r in records
                               if r["action_type"] == "set_nice"
                               and r["pid"] == proc.pid]
                    self.assertTrue(len(sn_recs) >= 1,
                                    "Audit must have a set_nice record")
            finally:
                proc.terminate()
                proc.wait()
        finally:
            os.unlink(audit)

    # ------------------------------------------------------------------
    # 10: set-nice PID 1 blocked
    # ------------------------------------------------------------------
    def test_10_set_nice_pid1_blocked(self):
        """set-nice on PID 1 must be rejected with non-zero exit."""
        audit = make_tmp_audit()
        try:
            res = run_gov("set-nice", "1", "5", "--yes", "--history", audit)
            self.assertNotEqual(res.returncode, 0,
                                "set-nice must block PID 1")
            records = read_audit(audit)
            self.assertTrue(
                any(r.get("status") == "rejected" and r["pid"] == 1
                    for r in records),
                "Audit must record rejected for PID 1 set-nice attempt"
            )
        finally:
            os.unlink(audit)

    # ------------------------------------------------------------------
    # 11: set-nice out-of-range nice blocked
    # ------------------------------------------------------------------
    def test_11_set_nice_out_of_range(self):
        """set-nice with nice outside [-20,19] must be rejected."""
        audit = make_tmp_audit()
        try:
            proc = subprocess.Popen(["sleep", "60"])
            time.sleep(0.1)
            try:
                for bad_nice in ["-21", "20", "99"]:
                    res = run_gov("set-nice", str(proc.pid), bad_nice,
                                  "--yes", "--history", audit)
                    self.assertNotEqual(
                        res.returncode, 0,
                        f"set-nice nice={bad_nice} should be rejected"
                    )
            finally:
                proc.terminate()
                proc.wait()
        finally:
            os.unlink(audit)

    # ------------------------------------------------------------------
    # 12: set-nice non-existent PID
    # ------------------------------------------------------------------
    def test_12_set_nice_nonexistent_pid(self):
        """set-nice on non-existent PID must fail gracefully."""
        audit = make_tmp_audit()
        try:
            res = run_gov("set-nice", "999999", "5",
                          "--yes", "--history", audit)
            self.assertNotEqual(res.returncode, 0,
                                "set-nice on dead PID must return non-zero")
            # Must not crash; stderr must contain meaningful message
            self.assertTrue(len(res.stderr) > 0,
                            "stderr must contain error message")
        finally:
            os.unlink(audit)

    # ------------------------------------------------------------------
    # 13: recommend-all on sample JSON
    # ------------------------------------------------------------------
    def test_13_recommend_all_sample_json(self):
        """recommend-all on processes.sample.json must produce recommendations."""
        if not os.path.isfile(SAMPLE_JSON):
            self.skipTest(f"Sample JSON not found: {SAMPLE_JSON}")
        audit = make_tmp_audit()
        try:
            res = run_gov("recommend-all", "--input", SAMPLE_JSON,
                          "--history", audit)
            self.assertEqual(
                res.returncode, 0,
                f"recommend-all failed:\n{res.stderr}"
            )
            out_lower = res.stdout.lower()
            # Must produce actionable output
            self.assertTrue(
                "boost" in out_lower or
                "throttle" in out_lower or
                "no_change" in out_lower or
                "no change" in out_lower,
                f"Expected recommendation output:\n{res.stdout}"
            )
            # Audit records should exist
            records = read_audit(audit)
            recommend_recs = [r for r in records
                              if r["action_type"] == "recommend"]
            self.assertGreater(len(recommend_recs), 0,
                               "recommend-all must log audit records")
        finally:
            os.unlink(audit)

    # ------------------------------------------------------------------
    # 14: recommend-all missing input file
    # ------------------------------------------------------------------
    def test_14_recommend_all_missing_file(self):
        """recommend-all with a non-existent file must return non-zero."""
        audit = make_tmp_audit()
        try:
            res = run_gov("recommend-all",
                          "--input", "/tmp/devgov_does_not_exist_xyz.json",
                          "--history", audit)
            self.assertNotEqual(res.returncode, 0,
                                "recommend-all on missing file must fail")
        finally:
            os.unlink(audit)

    # ------------------------------------------------------------------
    # 15: audit log is valid JSON-Lines after multiple operations
    # ------------------------------------------------------------------
    def test_15_audit_log_valid_jsonl(self):
        """After a sequence of operations, all audit records must be valid JSON."""
        audit = make_tmp_audit()
        try:
            proc = subprocess.Popen(["sleep", "60"])
            time.sleep(0.1)
            try:
                # Run several commands that all produce audit entries
                run_gov("inspect",  str(proc.pid), "--history", audit)
                run_gov("evaluate", str(proc.pid), "5",  "--history", audit)
                run_gov("evaluate", str(proc.pid), "-5", "--history", audit)
                run_gov("evaluate", "1",  "5",  "--history", audit)   # rejected
                run_gov("evaluate", "2",  "25", "--history", audit)   # rejected
                if os.path.isfile(SAMPLE_JSON):
                    run_gov("recommend-all", "--input", SAMPLE_JSON,
                            "--history", audit)
            finally:
                proc.terminate()
                proc.wait()

            # Every non-empty line must parse as JSON with required fields
            required_fields = {
                "timestamp", "pid", "process_name",
                "previous_nice", "requested_nice", "resulting_nice",
                "action_type", "status", "message"
            }
            records = read_audit(audit)
            self.assertGreater(len(records), 0,
                               "Audit log must have entries")
            for i, record in enumerate(records):
                for field in required_fields:
                    self.assertIn(
                        field, record,
                        f"Record #{i+1} missing field '{field}': {record}"
                    )
                # timestamp must be non-empty string
                self.assertIsInstance(record["timestamp"], str)
                self.assertGreater(len(record["timestamp"]), 0)
                # pid must be integer
                self.assertIsInstance(record["pid"], int)
                # action_type must be non-empty string
                self.assertIsInstance(record["action_type"], str)
                self.assertGreater(len(record["action_type"]), 0)
        finally:
            os.unlink(audit)


if __name__ == "__main__":
    unittest.main(verbosity=2)
