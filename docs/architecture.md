# DevGovernor Architecture & OS Concepts Guide

**Project Name:** DevGovernor — Linux Process-Monitoring and Scheduling-Priority Management Prototype  
**Course:** Operating Systems Lab (B.Tech Information Technology, 40-Mark Project)  
**Authors:** Team of 3 Students (Student 1: Process Monitor, Student 2: Governor, Student 3: Analytics Dashboard)

---

## 1. System Overview & Architecture

DevGovernor is a modular, user-space system software tool for Linux that inspects running processes, heuristically classifies workloads, computes accurate CPU/memory resource utilization, recommends priority tuning, and safely executes user-approved scheduling nice-value adjustments.

```
+-----------------------------------------------------------------------------------+
|                               Linux Kernel Space                                  |
|                                                                                   |
|  +--------------------+     +---------------------------+     +----------------+  |
|  | /proc Filesystem   |     | CFS CPU Scheduler         |     | setpriority()  |  |
|  | (/proc/[pid]/...)  |     | (Nice Values: -20 to 19)  |     | System Call    |  |
+--+---------+----------+-----+-------------+-------------+-----+-------+----------++
             |                              |                           |
             | (Virtual file reads)         | (Inspect / Evaluate)      | (Syscall)
             v                              v                           |
+---------------------------+  +---------------------------+            |
| Module 1: Monitor (C)     |  | Module 2: Governor (C)    +------------+
| (Student 1)               |  | (Student 2)               |
| - Scans /proc             |  | - inspect <PID>           |
| - Samples utime + stime   |  | - evaluate <PID> <NICE>   |
| - Workload heuristic tags |  | - set-nice <PID> <NICE>   |
| - Atomic JSON snapshot    |  | - Appends actions.jsonl   |
+-------------+-------------+  +-------------+-------------+
              |                              |
              | data/processes.json          | data/actions.jsonl
              +--------------+---------------+
                             | (Decoupled File-based IPC)
                             v
              +-------------------------------+
              | Module 3: Analytics (Python 3)|
              | (Student 3)                   |
              | - Real-time terminal table    |
              | - Workload distribution       |
              | - Governor audit & comparison |
              +-------------------------------+
```

### Module Responsibilities & Team Division

| Module | Primary Language | Owner | Key Responsibilities |
| :--- | :--- | :--- | :--- |
| **Module 1: Monitor** | C (C99 / POSIX) | **Student 1** | Traverses `/proc`, extracts PID/PPID/state/memory/nice, performs two-point CPU tick sampling, classifies workloads, and writes `data/processes.json` atomically. |
| **Module 2: Governor** | C (C99 / POSIX) | **Student 2** | Inspects scheduling parameters, evaluates proposed priority adjustments (dry-run), enforces safety guards (PID 1 protection, range validation), executes `setpriority()`, and writes audit records to `data/actions.jsonl`. |
| **Module 3: Analytics** | Python 3 (Standard Library) | **Student 3** | Parses monitor snapshot and action history, displays formatted terminal dashboards, aggregates workload metrics, and handles missing or corrupt data gracefully. |

---

## 2. Inter-Process Communication (IPC) Design

The architecture deliberately decouples the modules using **file-based IPC** instead of direct C-to-Python shared memory bindings.

### Rationale:
1. **Separation of Concerns:** Each student can develop, compile, test, and debug their module independently on separate Git branches without linking errors or ABI mismatches.
2. **Reliability & Crash Isolation:** A crash or restart of the dashboard does not interrupt the continuous monitoring or governor modules.
3. **Auditability:** Storing actions in `data/actions.jsonl` provides an immutable, transparent log for grading, debugging, and post-mortem review.

### Concurrency & Synchronization:
- **Atomic File Replacement:** To prevent Module 3 from reading partially written JSON files, Module 1 writes its data to a temporary file (`data/processes.json.tmp`) and uses the POSIX `rename()` system call. In Linux, `rename()` is an atomic operation within the same filesystem.
- **Append-Only Audit Log:** Module 2 opens `data/actions.jsonl` in `"a"` (append) mode. Each record is terminated by a newline, ensuring atomic line appends under POSIX write guarantees.

---

## 3. Data Contracts & Schemas

### Monitor Snapshot (`data/processes.json`)
Conforms to `shared/schema.json`:
```json
{
  "schema_version": 1,
  "timestamp": "2026-10-14T15:10:36Z",
  "sampling_interval_seconds": 0.20,
  "process_count": 31,
  "processes": [
    {
      "pid": 522,
      "ppid": 521,
      "name": "sleep",
      "state": "S",
      "cpu_percent": 0.0,
      "memory_mb": 1.75,
      "nice_value": 0,
      "workload_category": "background_process"
    }
  ]
}
```

### Governor Audit Record (`data/actions.jsonl`)
One JSON object per line:
```json
{"timestamp": "2026-10-14T15:11:00Z", "pid": 522, "process_name": "sleep", "previous_nice": 0, "requested_nice": 5, "resulting_nice": 5, "action_type": "set_nice", "status": "success", "message": "Successfully changed nice from 0 to 5"}
```

---

## 4. Operating Systems Concepts Explained (Viva Preparation)

### 4.1 The `/proc` Virtual Pseudo-Filesystem
In Linux, `/proc` does not reside on disk; it is an in-memory virtual filesystem maintained dynamically by the kernel.
- **`/proc/[pid]/status`**: Human-readable process metadata containing `Name`, `State`, `PPid`, and memory fields (`VmRSS`, `VmSize`).
- **`/proc/[pid]/stat`**: Machine-readable single-line status used by utilities like `ps` and `top`. It contains CPU accounting counters:
  - Field 14: `utime` — Amount of time scheduled in user mode (in clock ticks).
  - Field 15: `stime` — Amount of time scheduled in kernel mode (in clock ticks).
- **`/proc/[pid]/comm`**: The executable command name (up to 16 characters).

### 4.2 Process States & The Process Control Block (PCB)
The Linux kernel tracks each process via a `struct task_struct` (the Linux PCB). The monitor captures standard state characters:
- **`R` (Running / Runnable):** Process is currently on a CPU runqueue or executing on a core.
- **`S` (Interruptible Sleep):** Waiting for an event or I/O completion (e.g., waiting on keyboard input or network packet).
- **`D` (Uninterruptible Sleep):** Usually waiting for synchronous disk I/O; cannot be interrupted by signals.
- **`Z` (Zombie):** Process terminated, but parent has not yet called `wait()` / `waitpid()` to collect its exit code.
- **`T` (Stopped / Traced):** Process stopped by job control signal (`SIGSTOP`, `SIGTSTP`) or a debugger.

### 4.3 CPU Scheduling & Linux Nice Values
- **Completely Fair Scheduler (CFS):** The standard Linux scheduler balances CPU time across runnable tasks by tracking each process's *virtual runtime* (`vruntime`). CFS always picks the task with the smallest `vruntime` to execute next.
- **Nice Values:** Range from **`-20`** (highest priority) to **`+19`** (lowest priority). Default is **`0`**.
- **CFS Weight Formula:** A difference of 5 nice levels maps to approximately a factor of $3\times$ difference in CPU execution time.
- **Privilege Separation:**
  - Any user can **raise** their own process's nice value (lowering its scheduling priority) to be polite.
  - Only processes with the `CAP_SYS_NICE` Linux capability or the superuser (`root`) can **lower** nice values below the current level (granting more CPU share).

### 4.4 CPU Utilization Calculation (Two-Point Sampling)
Instantaneous CPU usage cannot be read as a static number from the kernel. It must be derived over a time delta $\Delta t$:
$$\Delta \text{ticks} = (\text{utime}_2 + \text{stime}_2) - (\text{utime}_1 + \text{stime}_1)$$
$$\text{CPU \%} = \frac{\Delta \text{ticks} / \text{CLK\_TCK}}{\Delta t} \times 100.0$$
where `CLK_TCK = sysconf(_SC_CLK_TCK)` (typically 100 ticks per second on x86_64 Linux).

### 4.5 Memory Metrics: RSS vs. Virtual Size
- **`VmRSS` (Resident Set Size):** The actual physical RAM currently occupied by the process. DevGovernor reports this metric because it accurately reflects real memory consumption.
- **`VmSize` (Virtual Size):** The total address space mapped by the process (including dynamically linked libraries, uncommitted pages, and memory-mapped files). A program might allocate 1 GB of virtual memory while only touching 20 MB of physical RAM.

### 4.6 Safety Guards & Error Handling
1. **Protection of PID 1:** PID 1 (`init` or `systemd`) manages the entire user-space lifecycle. Deprioritizing or terminating PID 1 can destabilize the operating system. DevGovernor rejects any modifications targeting PID 1.
2. **Ephemeral Processes:** In a multi-tasking operating system, processes terminate at arbitrary times. When the monitor calls `readdir("/proc")` and subsequently opens `/proc/[pid]/status`, the process might have exited. The code explicitly checks for `ENOENT` / `ESRCH` and skips gracefully without segmentation faults.
3. **`getpriority()` Return Ambiguity:** `getpriority()` returns an integer nice value. However, `-1` is a valid nice value! To distinguish an error from nice value `-1`, the caller must set `errno = 0` before the invocation and inspect `errno` after.
