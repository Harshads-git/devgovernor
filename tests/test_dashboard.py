#!/usr/bin/env python3
"""
================================================================================
DevGovernor — Unit Test Suite for Module 3 (Terminal Analytics Dashboard)
================================================================================
Student 3: Analytics Dashboard, Fault-Tolerant Data Ingestion, & Visualization

Course: Operating Systems Lab (B.Tech Information Technology)
Project: DevGovernor — Linux Developer Productivity & Scheduling Priority Governor

Validates:
  1. Fault-tolerant JSON data ingestion:
     - Normal valid JSON snapshots conforming to shared/schema.json
     - Non-existent files (clean error message, no crash)
     - Empty zero-byte files (sampling in progress detection)
     - Malformed/truncated JSON syntax recovery
     - Invalid schema formats (root is not a dict, missing 'processes' key)
  2. Action history JSON-lines ingestion:
     - Missing history file handling (clean empty list)
     - Empty history file handling
     - Graceful recovery and line skipping on corrupted JSON lines
  3. Workload analytics & system summary computation:
     - Process count, memory aggregation (RSS sum in MB)
     - Workload category distribution counts
     - Process state distributions (R, S, D, Z)
     - Priority distribution (elevated, standard, deprioritized)
     - Top CPU and memory consumer detection
     - Null CPU/memory value handling
  4. Process filtering and multi-column sorting:
     - Sort by CPU (descending)
     - Sort by memory (descending)
     - Sort by nice (ascending priority)
     - Sort by PID (ascending)
     - Filter by workload category
     - Entry limit truncation
  5. Visual progress bar & badge rendering:
     - Boundary conditions (0%, 50%, 100%, >100%, negative, null)
     - Unicode blocks and ASCII fallback characters
     - Fixed-width category badges
     - Process state descriptions
  6. Governor action correlation with live processes:
     - Applied status verification (live nice == resulting nice)
     - Pending status detection
     - Exited/terminated process handling
  7. Lab report generation:
     - Markdown report creation and verification of sections
  8. Command line argument parsing and smoke rendering test
================================================================================
"""

import os
import sys
import json
import tempfile
import unittest

# Add devgovernor root and module3_analytics to sys.path
TEST_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.abspath(os.path.join(TEST_DIR, ".."))
MODULE3_DIR = os.path.join(PROJECT_ROOT, "module3_analytics")
if MODULE3_DIR not in sys.path:
    sys.path.insert(0, MODULE3_DIR)

import dashboard


class TestDashboardDataIngestion(unittest.TestCase):
    """Test fault-tolerant file-based data ingestion (Zero-Crash Tolerance)."""

    def setUp(self):
        self.test_files = []

    def tearDown(self):
        for path in self.test_files:
            if os.path.exists(path):
                try:
                    os.remove(path)
                except Exception:
                    pass

    def _create_temp_file(self, content, suffix=".json"):
        with tempfile.NamedTemporaryFile(suffix=suffix, delete=False, mode="w", encoding="utf-8") as tf:
            tf.write(content)
            temp_path = tf.name
        self.test_files.append(temp_path)
        return temp_path

    def test_load_process_data_valid(self):
        """Valid processes JSON snapshot should parse cleanly."""
        sample_path = os.path.join(PROJECT_ROOT, "data", "processes.sample.json")
        data, err = dashboard.load_process_data(sample_path)
        self.assertIsNone(err)
        self.assertIsNotNone(data)
        self.assertIn("processes", data)
        self.assertIsInstance(data["processes"], list)
        self.assertGreater(len(data["processes"]), 0)

    def test_load_process_data_nonexistent(self):
        """Non-existent file must return None and descriptive error without raising."""
        fake_path = os.path.join(PROJECT_ROOT, "data", "non_existent_file_devgov_xyz.json")
        data, err = dashboard.load_process_data(fake_path)
        self.assertIsNone(data)
        self.assertIsNotNone(err)
        self.assertIn("not found", err.lower())

    def test_load_process_data_empty(self):
        """Zero-byte file must return None and descriptive empty message."""
        empty_file = self._create_temp_file("")
        data, err = dashboard.load_process_data(empty_file, retries=0)
        self.assertIsNone(data)
        self.assertIsNotNone(err)
        self.assertIn("empty", err.lower())

    def test_load_process_data_malformed_json(self):
        """Malformed JSON content must be intercepted and reported gracefully."""
        corrupt_file = self._create_temp_file("{ 'invalid_json': true, unfinished_key: ")
        data, err = dashboard.load_process_data(corrupt_file, retries=0)
        self.assertIsNone(data)
        self.assertIsNotNone(err)
        self.assertIn("malformed json", err.lower())

    def test_load_process_data_non_dict_root(self):
        """Root JSON element that is a list instead of dict must be rejected safely."""
        bad_format_file = self._create_temp_file("[1, 2, 3]")
        data, err = dashboard.load_process_data(bad_format_file, retries=0)
        self.assertIsNone(data)
        self.assertIsNotNone(err)
        self.assertIn("must be a json object", err.lower())

    def test_load_process_data_missing_processes_key(self):
        """JSON object missing 'processes' list must return descriptive error."""
        missing_key_file = self._create_temp_file('{"timestamp": "2026-10-14T00:00:00Z"}')
        data, err = dashboard.load_process_data(missing_key_file, retries=0)
        self.assertIsNone(data)
        self.assertIsNotNone(err)
        self.assertIn("missing 'processes' list", err.lower())

    def test_load_action_history_nonexistent(self):
        """Non-existent history file must return empty list without error."""
        fake_hist = os.path.join(PROJECT_ROOT, "data", "non_existent_actions_xyz.jsonl")
        actions, err = dashboard.load_action_history(fake_hist)
        self.assertEqual(actions, [])
        self.assertIsNone(err)

    def test_load_action_history_empty(self):
        """Empty history file must return empty list."""
        empty_hist = self._create_temp_file("", suffix=".jsonl")
        actions, err = dashboard.load_action_history(empty_hist)
        self.assertEqual(actions, [])
        self.assertIsNone(err)

    def test_load_action_history_with_corrupt_lines(self):
        """Corrupt lines in JSON-lines audit file must be skipped cleanly."""
        content = (
            '{"timestamp": "2026-10-14T10:00:00Z", "pid": 101, "action_type": "inspect"}\n'
            'THIS LINE IS CORRUPT\n'
            '\n'
            '{"timestamp": "2026-10-14T10:01:00Z", "pid": 102, "action_type": "evaluate"}\n'
            'SECOND CORRUPTED LINE {{}}\n'
            '{"timestamp": "2026-10-14T10:02:00Z", "pid": 103, "action_type": "set_nice"}\n'
        )
        hist_file = self._create_temp_file(content, suffix=".jsonl")
        actions, err = dashboard.load_action_history(hist_file, max_records=10)
        self.assertIsNone(err)
        self.assertEqual(len(actions), 3)
        self.assertEqual(actions[0]["pid"], 101)
        self.assertEqual(actions[1]["pid"], 102)
        self.assertEqual(actions[2]["pid"], 103)


class TestDashboardAnalytics(unittest.TestCase):
    """Test OS workload aggregation, sorting, and filtering logic."""

    def setUp(self):
        self.sample_snapshot = {
            "schema_version": 1,
            "timestamp": "2026-10-14T12:00:00Z",
            "sampling_interval_seconds": 1.0,
            "process_count": 5,
            "processes": [
                {
                    "pid": 100,
                    "ppid": 1,
                    "name": "code",
                    "state": "S",
                    "cpu_percent": 15.0,
                    "memory_mb": 200.0,
                    "nice_value": -2,
                    "workload_category": "development_tool"
                },
                {
                    "pid": 200,
                    "ppid": 100,
                    "name": "gcc",
                    "state": "R",
                    "cpu_percent": 90.0,
                    "memory_mb": 150.0,
                    "nice_value": 0,
                    "workload_category": "build_process"
                },
                {
                    "pid": 300,
                    "ppid": 1,
                    "name": "python3",
                    "state": "S",
                    "cpu_percent": 25.0,
                    "memory_mb": 100.0,
                    "nice_value": 0,
                    "workload_category": "development_server"
                },
                {
                    "pid": 400,
                    "ppid": 1,
                    "name": "rsyslogd",
                    "state": "S",
                    "cpu_percent": 0.5,
                    "memory_mb": 20.0,
                    "nice_value": 5,
                    "workload_category": "background_process"
                },
                {
                    "pid": 500,
                    "ppid": 1,
                    "name": "defunct_worker",
                    "state": "Z",
                    "cpu_percent": None,
                    "memory_mb": None,
                    "nice_value": 0,
                    "workload_category": "system_or_unknown"
                }
            ]
        }

    def test_calculate_system_summary(self):
        """Verify OS summary metrics aggregation across processes."""
        summary = dashboard.calculate_system_summary(self.sample_snapshot)
        self.assertEqual(summary["total_processes"], 5)
        # Sum of 200 + 150 + 100 + 20 = 470.0 MB
        self.assertAlmostEqual(summary["total_rss_mb"], 470.0, places=1)
        # Mean CPU across measured processes: (15 + 90 + 25 + 0.5) / 4 = 32.625%
        self.assertAlmostEqual(summary["avg_cpu_percent"], 32.625, places=2)

        # Workload counts
        wc = summary["workload_counts"]
        self.assertEqual(wc["development_tool"], 1)
        self.assertEqual(wc["build_process"], 1)
        self.assertEqual(wc["development_server"], 1)
        self.assertEqual(wc["background_process"], 1)
        self.assertEqual(wc["system_or_unknown"], 1)

        # Process states
        sc = summary["state_counts"]
        self.assertEqual(sc["R"], 1)
        self.assertEqual(sc["S"], 3)
        self.assertEqual(sc["Z"], 1)

        # Priority breakdown
        pc = summary["priority_counts"]
        self.assertEqual(pc["elevated"], 1)      # nice -2
        self.assertEqual(pc["standard"], 3)      # nice 0
        self.assertEqual(pc["deprioritized"], 1)  # nice 5

        # Peak consumers
        self.assertEqual(summary["top_cpu_proc"]["pid"], 200)   # gcc at 90%
        self.assertEqual(summary["top_mem_proc"]["pid"], 100)   # code at 200MB

    def test_sorting_by_cpu(self):
        """Sort by CPU descending; nulls must sort to the bottom."""
        procs = self.sample_snapshot["processes"]
        sorted_procs = dashboard.filter_and_sort_processes(procs, sort_by="cpu")
        self.assertEqual(sorted_procs[0]["pid"], 200)  # 90%
        self.assertEqual(sorted_procs[1]["pid"], 300)  # 25%
        self.assertEqual(sorted_procs[2]["pid"], 100)  # 15%
        self.assertEqual(sorted_procs[3]["pid"], 400)  # 0.5%
        self.assertEqual(sorted_procs[4]["pid"], 500)  # None

    def test_sorting_by_memory(self):
        """Sort by Memory descending."""
        procs = self.sample_snapshot["processes"]
        sorted_procs = dashboard.filter_and_sort_processes(procs, sort_by="memory")
        self.assertEqual(sorted_procs[0]["pid"], 100)  # 200 MB
        self.assertEqual(sorted_procs[1]["pid"], 200)  # 150 MB
        self.assertEqual(sorted_procs[2]["pid"], 300)  # 100 MB

    def test_sorting_by_nice(self):
        """Sort by Nice ascending (lower nice = higher priority at top)."""
        procs = self.sample_snapshot["processes"]
        sorted_procs = dashboard.filter_and_sort_processes(procs, sort_by="nice")
        self.assertEqual(sorted_procs[0]["pid"], 100)   # Nice -2
        self.assertEqual(sorted_procs[-1]["pid"], 400)  # Nice +5

    def test_sorting_by_pid(self):
        """Sort by PID ascending."""
        procs = self.sample_snapshot["processes"]
        sorted_procs = dashboard.filter_and_sort_processes(procs, sort_by="pid")
        self.assertEqual(sorted_procs[0]["pid"], 100)
        self.assertEqual(sorted_procs[-1]["pid"], 500)

    def test_filter_by_category(self):
        """Filter by workload category should retain only matching items."""
        procs = self.sample_snapshot["processes"]
        filtered = dashboard.filter_and_sort_processes(procs, filter_cat="build_process")
        self.assertEqual(len(filtered), 1)
        self.assertEqual(filtered[0]["name"], "gcc")

    def test_limit_truncation(self):
        """Limit parameter should truncate the process list."""
        procs = self.sample_snapshot["processes"]
        limited = dashboard.filter_and_sort_processes(procs, limit=2)
        self.assertEqual(len(limited), 2)


class TestDashboardVisualizationAndFormatting(unittest.TestCase):
    """Test progress bar rendering, badges, and state descriptions."""

    def test_render_progress_bar_unicode(self):
        """Test Unicode progress bar output at various percentages."""
        bar_0 = dashboard.render_progress_bar(0.0, width=10, ascii_only=False)
        self.assertIn("░░░░░░░░░░", bar_0)
        self.assertIn("0.0%", bar_0)

        bar_50 = dashboard.render_progress_bar(50.0, width=10, ascii_only=False)
        self.assertIn("█████░░░░░", bar_50)
        self.assertIn("50.0%", bar_50)

        bar_100 = dashboard.render_progress_bar(100.0, width=10, ascii_only=False)
        self.assertIn("██████████", bar_100)
        self.assertIn("100.0%", bar_100)

        # Multi-core overflow >100%
        bar_150 = dashboard.render_progress_bar(150.0, width=10, ascii_only=False)
        self.assertIn("██████████", bar_150)
        self.assertIn("+", bar_150)
        self.assertIn("150.0%", bar_150)

        # Null or negative
        bar_null = dashboard.render_progress_bar(None, width=10)
        self.assertIn("null", bar_null)

    def test_render_progress_bar_ascii(self):
        """Test ASCII fallback characters '=' and '.'."""
        bar_ascii = dashboard.render_progress_bar(40.0, width=10, ascii_only=True)
        self.assertIn("====", bar_ascii)
        self.assertIn("......", bar_ascii)
        self.assertIn("40.0%", bar_ascii)

    def test_format_category_badge(self):
        """Category badges must be formatted with consistent brackets."""
        self.assertEqual(dashboard.format_category_badge("development_tool"), "[DEV-TOOL]")
        self.assertEqual(dashboard.format_category_badge("development_server"), "[DEV-SRV ]")
        self.assertEqual(dashboard.format_category_badge("build_process"), "[BUILD   ]")
        self.assertEqual(dashboard.format_category_badge("background_process"), "[BG-PROC ]")
        self.assertEqual(dashboard.format_category_badge("system_or_unknown"), "[SYS/UNK ]")

    def test_format_state_label(self):
        """Process state codes must map to descriptive OS names."""
        self.assertIn("Running", dashboard.format_state_label("R"))
        self.assertIn("Interruptible Sleep", dashboard.format_state_label("S"))
        self.assertIn("Disk", dashboard.format_state_label("D"))
        self.assertIn("Zombie", dashboard.format_state_label("Z"))


class TestGovernorCorrelationAndReport(unittest.TestCase):
    """Test governor action correlation with live snapshots and markdown export."""

    def setUp(self):
        self.live_procs = [
            {"pid": 512, "name": "rsyslogd", "nice_value": 5},
            {"pid": 1042, "name": "code", "nice_value": 0},
        ]
        self.actions = [
            {
                "timestamp": "2026-10-14T15:55:00Z",
                "pid": 1042,
                "process_name": "code",
                "action_type": "inspect",
                "status": "success",
                "previous_nice": 0,
                "resulting_nice": 0,
                "message": "Inspected process"
            },
            {
                "timestamp": "2026-10-14T15:57:00Z",
                "pid": 512,
                "process_name": "rsyslogd",
                "action_type": "set_nice",
                "status": "success",
                "previous_nice": 0,
                "resulting_nice": 5,
                "message": "Changed nice from 0 to 5"
            },
            {
                "timestamp": "2026-10-14T15:58:00Z",
                "pid": 9999,
                "process_name": "old_tool",
                "action_type": "set_nice",
                "status": "success",
                "previous_nice": 0,
                "resulting_nice": 10,
                "message": "Changed nice"
            }
        ]

    def test_correlate_governor_actions(self):
        """Cross-reference actions with live snapshot to verify priority changes."""
        corr = dashboard.correlate_governor_actions(self.actions, self.live_procs)
        self.assertEqual(len(corr), 3)

        # Action 0: inspect on code (PID 1042, live nice=0)
        self.assertEqual(corr[0]["sync_status"], "OBSERVED")
        self.assertIsNotNone(corr[0]["live_proc"])

        # Action 1: set_nice 5 on rsyslogd (PID 512, live nice=5) -> APPLIED
        self.assertEqual(corr[1]["sync_status"], "APPLIED")
        self.assertEqual(corr[1]["live_proc"]["nice_value"], 5)

        # Action 2: set_nice on PID 9999 (not in live snapshot) -> EXITED
        self.assertEqual(corr[2]["sync_status"], "EXITED")
        self.assertIsNone(corr[2]["live_proc"])

    def test_export_markdown_report(self):
        """Markdown report exporter should write valid, structured markdown."""
        snapshot = {
            "schema_version": 1,
            "timestamp": "2026-10-14T16:00:00Z",
            "sampling_interval_seconds": 1.0,
            "process_count": 2,
            "processes": self.live_procs
        }

        with tempfile.NamedTemporaryFile(suffix=".md", delete=False) as tf:
            report_file = tf.name

        try:
            ok = dashboard.export_markdown_report(snapshot, self.actions, report_file)
            self.assertTrue(ok)
            self.assertTrue(os.path.exists(report_file))

            with open(report_file, "r", encoding="utf-8") as f:
                content = f.read()

            # Verify crucial report sections
            self.assertIn("# DevGovernor — Linux Process & Scheduling Priority Report", content)
            self.assertIn("## 1. Executive System Summary", content)
            self.assertIn("## 2. Workload Category & Scheduling Priority Breakdown", content)
            self.assertIn("## 3. Active Process Table Snapshot", content)
            self.assertIn("## 4. Governor Action History & Scheduling Priority Audit", content)
            self.assertIn("## 5. Technical Architecture & Viva Evaluation Notes", content)
            self.assertIn("Completely Fair Scheduler", content)
            self.assertIn("Resident Set Size", content)
            self.assertIn("rename()", content)

        finally:
            if os.path.exists(report_file):
                os.remove(report_file)


class TestDashboardCLISmoke(unittest.TestCase):
    """Smoke test command-line arguments and terminal rendering without exceptions."""

    def test_parse_arguments_defaults(self):
        """CLI parser should configure sensible defaults."""
        args = dashboard.parse_arguments([])
        self.assertEqual(args.input, "data/processes.json")
        self.assertEqual(args.history, "data/actions.jsonl")
        self.assertEqual(args.sort, "cpu")
        self.assertIsNone(args.filter)
        self.assertFalse(args.watch)
        self.assertEqual(args.refresh_sec, 2.0)

    def test_parse_arguments_custom(self):
        """CLI parser should correctly parse custom flags."""
        args = dashboard.parse_arguments([
            "--sort", "memory",
            "--filter", "build_process",
            "--limit", "5",
            "--no-clear",
            "--ascii",
            "--export-report", "test.md"
        ])
        self.assertEqual(args.sort, "memory")
        self.assertEqual(args.filter, "build_process")
        self.assertEqual(args.limit, 5)
        self.assertTrue(args.no_clear)
        self.assertTrue(args.ascii)
        self.assertEqual(args.export_report, "test.md")

    def test_render_dashboard_smoke(self):
        """Smoke test render_dashboard to ensure zero unhandled exceptions."""
        sample_path = os.path.join(PROJECT_ROOT, "data", "processes.sample.json")
        hist_path = os.path.join(PROJECT_ROOT, "data", "actions.sample.jsonl")

        proc_data, err = dashboard.load_process_data(sample_path)
        self.assertIsNone(err)
        actions, hist_err = dashboard.load_action_history(hist_path)
        self.assertIsNone(hist_err)

        # Call rendering with clear_screen=False to avoid ANSI sequences in test output
        dashboard.render_dashboard(
            proc_data,
            actions,
            sort_by="cpu",
            limit=5,
            filter_cat=None,
            clear_screen=False,
            ascii_only=True
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
