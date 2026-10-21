# DevGovernor — Linux Process & Scheduling Priority Report

> **Course:** Operating Systems Lab (B.Tech Information Technology)
> **Generated at:** 2026-10-21 13:15:55 UTC
> **Data Source Snapshot:** 2026-10-21T13:15:55Z (Sampling interval: 1.0s)

---

## 1. Executive System Summary

| Metric | Value | Technical OS Context |
| :--- | :--- | :--- |
| **Total Monitored Processes** | 30 | Process Control Blocks (PCBs) tracked in `/proc` |
| **Total Resident Memory (RSS)** | 492.7 MB (0.48 GB) | Physical pages committed to RAM |
| **Mean CPU Utilization** | 0.00% | CPU time delta accounting across sampling interval |
| **Peak CPU Consumer** | PID 1 (`systemd`) — 0.0% | Prime candidate for governor priority evaluation |
| **Peak Memory Consumer** | PID 279 (`openclaw-gatewa`) — 331.4 MB | Highest physical frame consumer in user space |

## 2. Workload Category & Scheduling Priority Breakdown

### 2.1 Workload Classification Distribution

| Category Badge | Workload Classification | Process Count | Percentage |
| :--- | :--- | :---: | :---: |
| `[DEV-TOOL]` | `development_tool` | 0 | 0.0% |
| `[DEV-SRV ]` | `development_server` | 0 | 0.0% |
| `[BUILD   ]` | `build_process` | 4 | 13.3% |
| `[BG-PROC ]` | `background_process` | 12 | 40.0% |
| `[SYS/UNK ]` | `system_or_unknown` | 14 | 46.7% |

### 2.2 Process Execution States & Runqueue Mapping

| State Code | Meaning | Count | Operating System Scheduling Queue |
| :---: | :--- | :---: | :--- |
| **R** | Running or Runnable | 1 | Active in Completely Fair Scheduler (CFS) red-black tree runqueue |
| **S** | Interruptible Sleep | 29 | Waiting in OS wait-queue (wake-up on I/O, IPC signal, or timer) |
| **D** | Disk/Uninterruptible Sleep | 0 | Blocked on hardware page fault or synchronous block device I/O |
| **Z** | Zombie / Defunct | 0 | Process exited; PCB retained pending parent `waitpid()` reaper |

## 3. Active Process Table Snapshot

*Sorted by: `CPU` | Category Filter: `ALL`*

| PID | PPID | Name | Workload Category | State | Nice | CPU (%) | RSS Memory (MB) |
| :---: | :---: | :--- | :--- | :---: | :---: | :---: | :---: |
| 1 | 0 | `systemd` | `background_process` | `S` | 0 | 0.0% | 12.1 |
| 2 | 1 | `init-systemd(Ub` | `background_process` | `S` | 0 | 0.0% | 1.9 |
| 7 | 2 | `init` | `system_or_unknown` | `S` | 0 | 0.0% | 1.8 |
| 43 | 1 | `systemd-journal` | `background_process` | `S` | -1 | 0.0% | 15.0 |
| 90 | 1 | `systemd-udevd` | `background_process` | `S` | 0 | 0.0% | 6.2 |
| 158 | 1 | `systemd-resolve` | `background_process` | `S` | 0 | 0.0% | 12.5 |
| 161 | 1 | `systemd-timesyn` | `background_process` | `S` | 0 | 0.0% | 7.4 |
| 174 | 1 | `cron` | `background_process` | `S` | 0 | 0.0% | 2.5 |
| 175 | 1 | `dbus-daemon` | `background_process` | `S` | 0 | 0.0% | 4.8 |
| 182 | 1 | `systemd-logind` | `background_process` | `S` | 0 | 0.0% | 8.2 |
| 184 | 1 | `wsl-pro-service` | `background_process` | `S` | 0 | 0.0% | 12.4 |
| 186 | 1 | `agetty` | `system_or_unknown` | `S` | 0 | 0.0% | 1.9 |
| 190 | 1 | `rsyslogd` | `background_process` | `S` | 0 | 0.0% | 5.1 |
| 196 | 1 | `agetty` | `system_or_unknown` | `S` | 0 | 0.0% | 1.8 |
| 207 | 1 | `unattended-upgr` | `system_or_unknown` | `S` | 0 | 0.0% | 22.1 |
| 218 | 1 | `systemd` | `background_process` | `S` | 0 | 0.0% | 10.5 |
| 222 | 218 | `(sd-pam)` | `system_or_unknown` | `S` | 0 | 0.0% | 3.4 |
| 279 | 218 | `openclaw-gatewa` | `system_or_unknown` | `S` | 0 | 0.0% | 331.4 |
| 340 | 2 | `SessionLeader` | `system_or_unknown` | `S` | 0 | 0.0% | 0.9 |
| 341 | 340 | `Relay(342)` | `system_or_unknown` | `S` | 0 | 0.0% | 1.0 |
| 342 | 341 | `bash` | `build_process` | `S` | 0 | 0.0% | 4.9 |
| 343 | 2 | `login` | `system_or_unknown` | `S` | 0 | 0.0% | 4.0 |
| 395 | 343 | `bash` | `build_process` | `S` | 0 | 0.0% | 5.1 |
| 546 | 342 | `monitor` | `system_or_unknown` | `R` | 0 | 0.0% | 1.8 |
| 549 | 2 | `SessionLeader` | `system_or_unknown` | `S` | 0 | 0.0% | 0.9 |

## 4. Governor Action History & Scheduling Priority Audit

| Timestamp (UTC) | PID | Process Name | Action | Status | Nice (Old → New) | Live Snapshot Verification |
| :--- | :---: | :--- | :---: | :---: | :---: | :--- |
| 2026-10-21T13:11:52Z | 196 | `agetty` | `recommend` | `recommendation` | 0 → None | **OBSERVED** (Live Nice = 0) |
| 2026-10-21T13:11:52Z | 207 | `unattended-upgr` | `recommend` | `recommendation` | 0 → None | **OBSERVED** (Live Nice = 0) |
| 2026-10-21T13:11:52Z | 218 | `systemd` | `recommend` | `recommendation` | 0 → None | **OBSERVED** (Live Nice = 0) |
| 2026-10-21T13:11:52Z | 222 | `(sd-pam)` | `recommend` | `recommendation` | 0 → None | **OBSERVED** (Live Nice = 0) |
| 2026-10-21T13:11:52Z | 279 | `openclaw-gatewa` | `recommend` | `recommendation` | 0 → None | **OBSERVED** (Live Nice = 0) |
| 2026-10-21T13:11:52Z | 340 | `SessionLeader` | `recommend` | `recommendation` | 0 → None | **OBSERVED** (Live Nice = 0) |
| 2026-10-21T13:11:52Z | 341 | `Relay(342)` | `recommend` | `recommendation` | 0 → None | **OBSERVED** (Live Nice = 0) |
| 2026-10-21T13:11:52Z | 342 | `bash` | `recommend` | `recommendation` | 0 → None | **OBSERVED** (Live Nice = 0) |
| 2026-10-21T13:11:52Z | 343 | `login` | `recommend` | `recommendation` | 0 → None | **OBSERVED** (Live Nice = 0) |
| 2026-10-21T13:11:52Z | 395 | `bash` | `recommend` | `recommendation` | 0 → None | **OBSERVED** (Live Nice = 0) |
| 2026-10-21T13:11:52Z | 546 | `monitor` | `recommend` | `recommendation` | 0 → None | **OBSERVED** (Live Nice = 0) |
| 2026-10-21T13:11:52Z | 549 | `SessionLeader` | `recommend` | `recommendation` | 0 → None | **OBSERVED** (Live Nice = 0) |
| 2026-10-21T13:11:52Z | 550 | `Relay(555)` | `recommend` | `recommendation` | 0 → None | **OBSERVED** (Live Nice = 0) |
| 2026-10-21T13:11:52Z | 555 | `bash` | `recommend` | `recommendation` | 0 → None | **OBSERVED** (Live Nice = 0) |
| 2026-10-21T13:14:19Z | 591 | `sleep` | `set_nice` | `success` | 0 → 10 | **EXITED** (Process Terminated) |

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

