#!/usr/bin/env python3
"""
============================================================================
DevGovernor — Lightweight Web Dashboard Server
============================================================================
Zero-dependency HTTP server using Python standard library.
Serves:
  1. Static Web Dashboard (HTML/CSS/JS) from web/ directory
  2. REST API endpoints:
     - GET  /api/processes  -> returns live data/processes.json
     - GET  /api/actions    -> returns live data/actions.jsonl as JSON array
     - POST /api/set-nice   -> executes ./module2_governor/governor set-nice
     - GET  /api/status     -> returns system & monitoring status
============================================================================
"""

import os
import sys
import json
import argparse
import subprocess
from http.server import SimpleHTTPRequestHandler, HTTPServer
from urllib.parse import urlparse, parse_qs

PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
WEB_DIR = os.path.join(PROJECT_ROOT, "web")
DATA_DIR = os.path.join(PROJECT_ROOT, "data")
PROCESSES_FILE = os.path.join(DATA_DIR, "processes.json")
PROCESSES_SAMPLE = os.path.join(DATA_DIR, "processes.sample.json")
ACTIONS_FILE = os.path.join(DATA_DIR, "actions.jsonl")
ACTIONS_SAMPLE = os.path.join(DATA_DIR, "actions.sample.jsonl")
GOVERNOR_BIN = os.path.join(PROJECT_ROOT, "module2_governor", "governor")


class DevGovernorHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=WEB_DIR, **kwargs)

    def end_headers(self):
        # Enable CORS and disable aggressive caching for real-time live data
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.send_header("Cache-Control", "no-cache, no-store, must-revalidate")
        super().end_headers()

    def do_OPTIONS(self):
        self.send_response(200)
        self.end_headers()

    def do_GET(self):
        parsed = urlparse(self.path)

        if parsed.path == "/api/processes":
            self.handle_get_processes()
        elif parsed.path == "/api/actions":
            self.handle_get_actions()
        elif parsed.path == "/api/status":
            self.handle_get_status()
        elif parsed.path in ("/", "/index.html"):
            self.path = "/index.html"
            super().do_GET()
        else:
            # Fallback to static file serving in web/
            super().do_GET()

    def do_POST(self):
        parsed = urlparse(self.path)
        if parsed.path == "/api/set-nice":
            self.handle_post_set_nice()
        else:
            self.send_error(404, "Endpoint not found")

    def handle_get_processes(self):
        target = PROCESSES_FILE if os.path.exists(PROCESSES_FILE) and os.path.getsize(PROCESSES_FILE) > 0 else PROCESSES_SAMPLE
        try:
            with open(target, "r", encoding="utf-8") as f:
                data = json.load(f)
            data["_source"] = "live" if target == PROCESSES_FILE else "sample"
            data["_file"] = os.path.basename(target)
            payload = json.dumps(data).encode("utf-8")

            self.send_response(200)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)
        except Exception as e:
            self.send_json_error(500, f"Error reading processes: {e}")

    def handle_get_actions(self):
        target = ACTIONS_FILE if os.path.exists(ACTIONS_FILE) else ACTIONS_SAMPLE
        records = []
        try:
            if os.path.exists(target):
                with open(target, "r", encoding="utf-8") as f:
                    for line in f:
                        line = line.strip()
                        if line:
                            try:
                                records.append(json.loads(line))
                            except json.JSONDecodeError:
                                continue
            payload = json.dumps({
                "count": len(records),
                "source": "live" if target == ACTIONS_FILE else "sample",
                "actions": records[-50:]  # Last 50 actions
            }).encode("utf-8")

            self.send_response(200)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)
        except Exception as e:
            self.send_json_error(500, f"Error reading actions: {e}")

    def handle_get_status(self):
        has_monitor_data = os.path.exists(PROCESSES_FILE) and os.path.getsize(PROCESSES_FILE) > 0
        has_governor_data = os.path.exists(ACTIONS_FILE) and os.path.getsize(ACTIONS_FILE) > 0
        has_governor_bin = os.path.exists(GOVERNOR_BIN)

        status_data = {
            "status": "online",
            "monitor_active": has_monitor_data,
            "governor_active": has_governor_data,
            "governor_binary_compiled": has_governor_bin,
            "platform": sys.platform,
            "project_name": "DevGovernor",
            "version": "1.0.0"
        }
        payload = json.dumps(status_data).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def handle_post_set_nice(self):
        content_length = int(self.headers.get("Content-Length", 0))
        post_data = self.rfile.read(content_length)
        try:
            body = json.loads(post_data.decode("utf-8"))
            pid = int(body.get("pid"))
            target_nice = int(body.get("nice"))

            if pid <= 1:
                return self.send_json_error(400, "Refused: PID <= 1 is protected (init/systemd)")
            if target_nice < -20 or target_nice > 19:
                return self.send_json_error(400, "Nice value must be in range [-20, 19]")

            # Run governor binary if available
            if os.path.exists(GOVERNOR_BIN) and sys.platform.startswith("linux"):
                cmd = [GOVERNOR_BIN, "set-nice", str(pid), str(target_nice), "--yes"]
                res = subprocess.run(cmd, capture_output=True, text=True, timeout=5)
                success = (res.returncode == 0)
                msg = res.stdout.strip() if success else res.stderr.strip()
            else:
                # Simulation fallback for non-Linux host or testing
                success = True
                msg = f"[Simulated Web Governor] Set priority of PID {pid} to nice {target_nice}"

            payload = json.dumps({
                "success": success,
                "pid": pid,
                "target_nice": target_nice,
                "message": msg
            }).encode("utf-8")

            self.send_response(200 if success else 400)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)

        except Exception as e:
            self.send_json_error(400, f"Invalid request: {e}")

    def send_json_error(self, code, message):
        payload = json.dumps({"error": message, "code": code}).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, format, *args):
        # Quiet console output
        sys.stderr.write(f"[DevGovernor Web] {format % args}\n")


def main():
    parser = argparse.ArgumentParser(description="DevGovernor Web Dashboard Server")
    parser.add_argument("--port", type=int, default=8080, help="Port to serve on (default: 8080)")
    parser.add_argument("--host", default="0.0.0.0", help="Host interface (default: 0.0.0.0)")
    args = parser.parse_args()

    server_address = (args.host, args.port)
    httpd = HTTPServer(server_address, DevGovernorHandler)

    print("=" * 72)
    print("      DevGovernor — Web Analytics Dashboard Server")
    print("=" * 72)
    print(f"  * Web UI URL       : http://localhost:{args.port}/")
    print(f"  * Network Access   : http://{args.host}:{args.port}/")
    print(f"  * Process API      : http://localhost:{args.port}/api/processes")
    print(f"  * Actions API      : http://localhost:{args.port}/api/actions")
    print("=" * 72)
    print("Press Ctrl+C to stop the web server.\n")

    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\n[DevGovernor Web] Server stopped cleanly.")
        httpd.server_close()


if __name__ == "__main__":
    main()
