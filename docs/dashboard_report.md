# DevGovernor — Linux Process & Scheduling Priority Report

> **Course:** Operating Systems Lab (B.Tech Information Technology)
> **Generated at:** 2026-10-14 17:46:51 UTC
> **Data Source Snapshot:** 2026-10-14T16:00:00Z (Sampling interval: 1.0s)

---

## 1. Executive System Summary

| Metric | Value | Technical OS Context |
| :--- | :--- | :--- |
| **Total Monitored Processes** | 8 | Process Control Blocks (PCBs) tracked in `/proc` |
| **Total Resident Memory (RSS)** | 647.5 MB (0.63 GB) | Physical pages committed to RAM |
| **Mean CPU Utilization** | 17.20% | CPU time delta accounting across sampling interval |
| **Peak CPU Consumer** | PID 1105 (`gcc`) — 88.2% | Prime candidate for governor priority evaluation |
| **Peak Memory Consumer** | PID 1340 (`node`) — 210.0 MB | Highest physical frame consumer in user space |

## 2. Workload Category & Scheduling Priority Breakdown

### 2.1 Workload Classification Distribution

| Category Badge | Workload Classification | Process Count | Percentage |
| :--- | :--- | :---: | :---: |
| `[DEV-TOOL]` | `development_tool` | 1 | 12.5% |
| `[DEV-SRV ]` | `development_server` | 2 | 25.0% |
| `[BUILD   ]` | `build_process` | 2 | 25.0% |
| `[BG-PROC ]` | `background_process` | 3 | 37.5% |
| `[SYS/UNK ]` | `system_or_unknown` | 0 | 0.0% |

### 2.2 Process Execution States & Runqueue Mapping

| State Code | Meaning | Count | Operating System Scheduling Queue |
| :---: | :--- | :---: | :--- |
| **R** | Running or Runnable | 1 | Active in Completely Fair Scheduler (CFS) red-black tree runqueue |
| **S** | Interruptible Sleep | 7 | Waiting in OS wait-queue (wake-up on I/O, IPC signal, or timer) |
| **D** | Disk/Uninterruptible Sleep | 0 | Blocked on hardware page fault or synchronous block device I/O |
| **Z** | Zombie / Defunct | 0 | Process exited; PCB retained pending parent `waitpid()` reaper |

## 3. Active Process Table Snapshot

*Sorted by: `CPU` | Category Filter: `ALL`*

| PID | PPID | Name | Workload Category | State | Nice | CPU (%) | RSS Memory (MB) |
| :---: | :---: | :--- | :--- | :---: | :---: | :---: | :---: |
| 1105 | 1042 | `gcc` | `build_process` | `R` | 0 | 88.2% | 94.6 |
| 1250 | 1000 | `python3` | `development_server` | `S` | 0 | 24.0% | 120.5 |
| 1042 | 1000 | `code` | `development_tool` | `S` | 0 | 14.5% | 182.4 |
| 1340 | 1000 | `node` | `development_server` | `S` | 0 | 8.5% | 210.0 |
| 1106 | 1042 | `make` | `build_process` | `S` | 0 | 2.1% | 14.2 |
| 512 | 1 | `rsyslogd` | `background_process` | `S` | 5 | 0.2% | 8.9 |
| 1 | 0 | `systemd` | `background_process` | `S` | 0 | 0.1% | 12.8 |
| 450 | 1 | `cron` | `background_process` | `S` | 0 | 0.0% | 4.1 |

## 4. Governor Action History & Scheduling Priority Audit

| Timestamp (UTC) | PID | Process Name | Action | Status | Nice (Old → New) | Live Snapshot Verification |
| :--- | :---: | :--- | :---: | :---: | :---: | :--- |
| 2026-10-14T15:55:00Z | 1042 | `code` | `inspect` | `success` | 0 → 0 | **OBSERVED** (Live Nice = 0) |
| 2026-10-14T15:56:00Z | 512 | `rsyslogd` | `evaluate` | `recommendation` | 0 → 0 | **RECOMMENDED** (Live Nice = 5) |
| 2026-10-14T15:57:00Z | 512 | `rsyslogd` | `set_nice` | `success` | 0 → 5 | **APPLIED** (Live Nice = 5) |

## 5. Technical Architecture & Viva Evaluation Notes

### 5.1 Inter-Process Communication (IPC) & Atomic File Replacement
To prevent race conditions between C background workers and Python consumers:
- Module 1 writes its JSON to a temporary file (`data/processes.json.tmp`) and executes `rename(tmp, target)`.
- In POSIX compliance, `rename()` is an atomic filesystem operation. Readers opening `processes.json` will either receive the complete old snapshot or the complete new snapshot, never an incomplete or torn file stream.

### 5.2 Resident Set Size (RSS) vs. Virtual Memory Size (VSZ)
- **VSZ (Virtual Size):** Represents total virtual address space allocated to the process, including mapped shared objects, code segments, and memory pages swapped out to disk.
- **RSS (Resident Set Size):** Represents physical RAM frames currently occupied by the process. DevGovernor prioritizes RSS because physical memory exhaustion triggers page thrashing and invokes the Linux Out-Of-Memory (OOM) killer.

### 5.3 Completely Fair Scheduler (CFS) & Nice Values
- In the Linux CFS scheduler, each task has a `vruntime` (virtual runtime). Tasks with the lowest `vruntime` are selected first.
- The `nice` value (-20 to +19) modulates the rate at which `vruntime` advances. Negative nice values (elevated priority) advance `vruntime` more slowly, granting more CPU timeslices, while positive nice values throttle background jobs.

