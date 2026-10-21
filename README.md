# DevGovernor — Linux Developer Productivity & Scheduling Priority Governor

> **Operating Systems Lab Project (B.Tech Information Technology — 40 Marks)**  
> **Team of Three Students**  
> **Target Environment:** Linux / Ubuntu (Native or via Windows 11 WSL2)  
> 🌐 **Live Web Dashboard & Simulator:** [https://harshads-git.github.io/devgovernor/](https://harshads-git.github.io/devgovernor/)

---

## 1. Project Overview & Clarification

**DevGovernor** is a lightweight, user-space developer productivity and resource-management system software built for Linux. It continuously monitors running processes, parses Linux `/proc` virtual files, classifies development workloads (editors, build tools, runtimes, background services), computes resource utilization, and allows explicit, safe scheduling nice-value adjustments to prioritize developer workflows.

> [!IMPORTANT]
> **Technical Scope Clarification:**  
> This project is a **user-space process-management and scheduling-priority governance tool** using standard Linux kernel facilities such as the `/proc` filesystem, POSIX process priorities, and `setpriority()` system calls. It **does not** modify the Linux kernel source code or implement a custom in-kernel scheduler.

---

## 2. Team Division & Responsibilities

| Role | Teammate | Key Deliverables & Git Boundary |
| :--- | :--- | :--- |
| **Student 1** | **Harshad S** ([@Harshads-git](https://github.com/Harshads-git))<br>*Process Monitoring & Workload Intelligence* | Reads `/proc/[pid]/status` and `/proc/[pid]/stat`, computes two-point CPU% utilization, measures RSS memory, implements heuristic classification, and produces atomic, validated JSON conforming to `shared/schema.json`. Located in `module1_monitor/`. |
| **Student 2** | **Mohnish Shanmugam** ([@Mohnish-Shanmugam](https://github.com/Mohnish-Shanmugam))<br>*Resource Management & Priority Governor* | Implements `inspect`, `evaluate` (dry-run recommendation), `set-nice` with `--yes` flag, CFS scheduling weights, workload policy recommendations (`recommend-all`), safety guards, and maintains append-only audit trail in `data/actions.jsonl`. Located in `module2_governor/`. |
| **Student 3** | **Girithar M** ([@GiritharM2007](https://github.com/GiritharM2007))<br>*Analytics & Terminal Dashboard* | Implements terminal user interface in Python 3. Renders live process tables, visual gauge bars, workload category distributions, top resource consumers, governor audit history, report generator, and handles missing/empty/corrupted files gracefully. Located in `module3_analytics/`. |

---

## 3. Technology Stack & Lightweight Architecture

- **Language (Modules 1 & 2):** C (C99 standard, POSIX 2008) compiled with `gcc`.
- **Language (Module 3 & Tests):** Python 3 (Strictly Standard Library: `os`, `sys`, `json`, `argparse`, `time`, `datetime`, `unittest`). Zero external `pip` dependencies required.
- **IPC Mechanism:** Decoupled file-based JSON contract (`data/processes.json` and `data/actions.jsonl`).
- **Target OS:** Ubuntu 22.04 / 24.04 on WSL2 or bare-metal Linux.

---

## 4. Quick Start & Build Instructions

### Prerequisites
Inside Ubuntu (WSL2 or native Linux):
```bash
sudo apt update
sudo apt install -y build-essential gcc make python3
```

### 1. Build All Components
From the `devgovernor/` root directory:
```bash
make
```
This builds both C binaries: `module1_monitor/monitor` and `module2_governor/governor`.

### 2. Run Automated Test Suite
```bash
make test
```
Runs comprehensive unit and integration tests verifying JSON compliance, safety guards, PID protection, boundary conditions, and corrupt file tolerance.

---

## 5. Usage & CLI Reference

### Module 1: Process Monitor (C)
```bash
# One-shot mode (takes a quick sample, exports data/processes.json, and exits)
./module1_monitor/monitor --output data/processes.json --oneshot

# Continuous monitoring mode (updates snapshot every 1 second)
./module1_monitor/monitor --output data/processes.json --interval 1
```

### Module 2: Priority Governor (C)
```bash
# 1. Inspect a process (display state, resident memory, and current nice priority)
./module2_governor/governor inspect <PID>

# 2. Evaluate a proposed change (dry-run recommendation, no modifications made)
./module2_governor/governor evaluate <PID> <TARGET_NICE>

# 3. Explicitly set nice priority (validated modification with audit logging)
./module2_governor/governor set-nice <PID> <TARGET_NICE>
```
*Note on Nice Range:* Nice values range from `-20` (highest CPU priority) to `19` (lowest priority). Unprivileged users can raise nice values (e.g., 0 to 10) to throttle background tasks. Lowering nice values (e.g., 0 to -5) requires superuser privileges (`sudo`).

### Module 3: Terminal Analytics Dashboard (Python 3)
```bash
# Standard single snapshot view
python3 module3_analytics/dashboard.py --input data/processes.json --history data/actions.jsonl

# Real-time continuous watch view (refreshes every 2 seconds)
python3 module3_analytics/dashboard.py --input data/processes.json --history data/actions.jsonl --watch

# Filter by workload category (e.g. show only build tools)
python3 module3_analytics/dashboard.py --filter build_process --sort memory

# Generate automated Markdown lab report for viva submission
python3 module3_analytics/dashboard.py --input data/processes.json --export-report docs/lab_viva_report.md
```

### Web Dashboard & Interactive Simulator (Browser UI)
```bash
# Start the lightweight web dashboard server (built with pure Python standard library)
make run-web
# OR directly:
python3 web/server.py --port 8080

# Then open in any browser:
# http://localhost:8080/
```
*Features:* Live process table with gauge bars, real-time metrics, interactive governor simulator (inspect, evaluate, set-nice), CFS weight delta calculations, and instant OS viva theory guide.



---

## 6. Directory Structure

```
devgovernor/
├── .gitignore                   # Ignores binaries, object files, runtime data
├── Makefile                     # Root build, test, and run coordinator
├── README.md                    # Project overview, setup, and viva guide
├── requirements.txt             # Dependency notes (zero external pip packages)
├── data/
│   ├── .gitkeep                 # Version-controlled data directory
│   ├── processes.json           # Live process snapshot (generated by Module 1)
│   └── actions.jsonl            # Audit log of governor actions (generated by Module 2)
├── docs/
│   ├── architecture.md          # In-depth system design & OS concept guide
│   └── testing.md               # Test matrix, manual test steps & benchmarks
├── module1_monitor/
│   ├── Makefile                 # Builds monitor binary
│   └── monitor.c                # C process scanner, CPU delta sampler, JSON exporter
├── module2_governor/
│   ├── Makefile                 # Builds governor binary
│   └── governor.c               # Priority inspection, evaluation & adjustment engine
├── module3_analytics/
│   └── dashboard.py             # Terminal user interface & analytics viewer
├── shared/
│   └── schema.json              # Formal JSON schema definition
└── tests/
    └── test_all.py              # Automated test suite (6 validation suites)
```

---

## 7. Viva Preparation — Questions & Answers

### Q1: How does your monitor extract process information without calling external commands like `ps` or `top`?
**Answer:** The monitor directly traverses the `/proc` virtual pseudo-filesystem. It calls `opendir("/proc")` and `readdir()`, filters for directory names that are purely numeric (representing PIDs), and opens `/proc/[pid]/status` for process metadata (Name, State, PPID, VmRSS) and `/proc/[pid]/stat` for raw CPU clock tick counters.

### Q2: Why can't we read CPU percentage directly as a static number from `/proc`?
**Answer:** The Linux kernel does not maintain an instantaneous "CPU percentage" for processes; it maintains cumulative time counters: `utime` (user mode clock ticks) and `stime` (kernel mode clock ticks). To compute CPU percentage, DevGovernor performs **two-point delta sampling**:
$$\text{CPU \%} = \frac{(\Delta\text{utime} + \Delta\text{stime}) / \text{CLK\_TCK}}{\Delta t} \times 100.0$$

### Q3: What is the difference between VmSize and VmRSS?
**Answer:**
- `VmSize` is the total virtual address space mapped by the process (including uncommitted memory and shared libraries).
- `VmRSS` (Resident Set Size) is the actual physical RAM currently occupied by the process. DevGovernor reports `VmRSS` because it reflects true memory pressure.

### Q4: What are Linux nice values, and how do they relate to the Completely Fair Scheduler (CFS)?
**Answer:** Nice values range from `-20` (highest priority) to `+19` (lowest priority, default `0`). In CFS, nice values are converted into scheduling weights. A lower nice value gives a task a higher weight, causing its virtual runtime (`vruntime`) to accumulate more slowly, so the scheduler picks it more frequently.

### Q5: Why is PID 1 protected in your governor?
**Answer:** PID 1 is the initialization process (`init` or `systemd`), the ancestor of all user-space processes. Altering its priority or signaling it could cause system instability, unresponsive hardware event handling, or kernel panics.

### Q6: How do you prevent race conditions between the C monitor writing the file and Python reading it?
**Answer:** Module 1 writes its JSON to a temporary file (`data/processes.json.tmp`) and then executes `rename()` to atomically replace `data/processes.json`. Under POSIX/Linux, `rename()` is an atomic operation within the same filesystem, guaranteeing that the Python dashboard never observes a partially written file.
