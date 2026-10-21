# DevGovernor — Linux Process & Scheduling Priority Report

> **Course:** Operating Systems Lab (B.Tech Information Technology)
> **Generated at:** 2026-10-21 12:58:07 UTC
> **Data Source Snapshot:** 2026-10-21T12:58:06Z (Sampling interval: 0.2s)

---

## 1. Executive System Summary

| Metric | Value | Technical OS Context |
| :--- | :--- | :--- |
| **Total Monitored Processes** | 33 | Process Control Blocks (PCBs) tracked in `/proc` |
| **Total Resident Memory (RSS)** | 671.6 MB (0.66 GB) | Physical pages committed to RAM |
| **Mean CPU Utilization** | 3.46% | CPU time delta accounting across sampling interval |
| **Peak CPU Consumer** | PID 274 (`openclaw-gatewa`) — 114.2% | Prime candidate for governor priority evaluation |
| **Peak Memory Consumer** | PID 274 (`openclaw-gatewa`) — 462.2 MB | Highest physical frame consumer in user space |

## 2. Workload Category & Scheduling Priority Breakdown

### 2.1 Workload Classification Distribution

| Category Badge | Workload Classification | Process Count | Percentage |
| :--- | :--- | :---: | :---: |
| `[DEV-TOOL]` | `development_tool` | 0 | 0.0% |
| `[DEV-SRV ]` | `development_server` | 0 | 0.0% |
| `[BUILD   ]` | `build_process` | 3 | 9.1% |
| `[BG-PROC ]` | `background_process` | 14 | 42.4% |
| `[SYS/UNK ]` | `system_or_unknown` | 16 | 48.5% |

### 2.2 Process Execution States & Runqueue Mapping

| State Code | Meaning | Count | Operating System Scheduling Queue |
| :---: | :--- | :---: | :--- |
| **R** | Running or Runnable | 2 | Active in Completely Fair Scheduler (CFS) red-black tree runqueue |
| **S** | Interruptible Sleep | 31 | Waiting in OS wait-queue (wake-up on I/O, IPC signal, or timer) |
| **D** | Disk/Uninterruptible Sleep | 0 | Blocked on hardware page fault or synchronous block device I/O |
| **Z** | Zombie / Defunct | 0 | Process exited; PCB retained pending parent `waitpid()` reaper |

## 3. Active Process Table Snapshot

*Sorted by: `CPU` | Category Filter: `ALL`*

| PID | PPID | Name | Workload Category | State | Nice | CPU (%) | RSS Memory (MB) |
| :---: | :---: | :--- | :--- | :---: | :---: | :---: | :---: |
| 274 | 218 | `openclaw-gatewa` | `system_or_unknown` | `R` | 0 | 114.2% | 462.2 |
| 1 | 0 | `systemd` | `background_process` | `S` | 0 | 0.0% | 12.0 |
| 2 | 1 | `init-systemd(Ub` | `background_process` | `S` | 0 | 0.0% | 2.0 |
| 7 | 2 | `init` | `system_or_unknown` | `S` | 0 | 0.0% | 1.8 |
| 43 | 1 | `systemd-journal` | `background_process` | `S` | -1 | 0.0% | 15.1 |
| 90 | 1 | `systemd-udevd` | `background_process` | `S` | 0 | 0.0% | 6.0 |
| 158 | 1 | `systemd-resolve` | `background_process` | `S` | 0 | 0.0% | 12.4 |
| 165 | 1 | `systemd-timesyn` | `background_process` | `S` | 0 | 0.0% | 7.5 |
| 174 | 1 | `cron` | `background_process` | `S` | 0 | 0.0% | 2.4 |
| 175 | 1 | `dbus-daemon` | `background_process` | `S` | 0 | 0.0% | 4.9 |
| 181 | 1 | `snapd` | `background_process` | `S` | 0 | 0.0% | 34.8 |
| 182 | 1 | `systemd-logind` | `background_process` | `S` | 0 | 0.0% | 8.0 |
| 184 | 1 | `wsl-pro-service` | `background_process` | `S` | 0 | 0.0% | 12.1 |
| 186 | 1 | `agetty` | `system_or_unknown` | `S` | 0 | 0.0% | 1.9 |
| 193 | 1 | `rsyslogd` | `background_process` | `S` | 0 | 0.0% | 4.9 |
| 196 | 1 | `agetty` | `system_or_unknown` | `S` | 0 | 0.0% | 1.8 |
| 207 | 1 | `unattended-upgr` | `system_or_unknown` | `S` | 0 | 0.0% | 21.9 |
| 218 | 1 | `systemd` | `background_process` | `S` | 0 | 0.0% | 10.9 |
| 221 | 218 | `(sd-pam)` | `system_or_unknown` | `S` | 0 | 0.0% | 3.4 |
| 276 | 1 | `systemd-timedat` | `background_process` | `S` | 0 | 0.0% | 7.0 |
| 342 | 2 | `login` | `system_or_unknown` | `S` | 0 | 0.0% | 4.1 |
| 382 | 342 | `bash` | `build_process` | `S` | 0 | 0.0% | 4.9 |
| 400 | 2 | `SessionLeader` | `system_or_unknown` | `S` | 0 | 0.0% | 0.9 |
| 401 | 400 | `Relay(407)` | `system_or_unknown` | `S` | 0 | 0.0% | 1.2 |
| 402 | 90 | `(udev-worker)` | `system_or_unknown` | `S` | 0 | 0.0% | 3.8 |

## 4. Governor Action History & Scheduling Priority Audit

| Timestamp (UTC) | PID | Process Name | Action | Status | Nice (Old → New) | Live Snapshot Verification |
| :--- | :---: | :--- | :---: | :---: | :---: | :--- |
| 2026-10-21T12:58:06Z | 274 | `openclaw-gatewa` | `recommend` | `recommendation` | 0 → None | **OBSERVED** (Live Nice = 0) |
| 2026-10-21T12:58:06Z | 276 | `systemd-timedat` | `recommend` | `recommendation` | 0 → None | **OBSERVED** (Live Nice = 0) |
| 2026-10-21T12:58:06Z | 342 | `login` | `recommend` | `recommendation` | 0 → None | **OBSERVED** (Live Nice = 0) |
| 2026-10-21T12:58:06Z | 382 | `bash` | `recommend` | `recommendation` | 0 → None | **OBSERVED** (Live Nice = 0) |
| 2026-10-21T12:58:06Z | 400 | `SessionLeader` | `recommend` | `recommendation` | 0 → None | **OBSERVED** (Live Nice = 0) |
| 2026-10-21T12:58:06Z | 401 | `Relay(407)` | `recommend` | `recommendation` | 0 → None | **OBSERVED** (Live Nice = 0) |
| 2026-10-21T12:58:06Z | 402 | `(udev-worker)` | `recommend` | `recommendation` | 0 → None | **OBSERVED** (Live Nice = 0) |
| 2026-10-21T12:58:06Z | 403 | `(udev-worker)` | `recommend` | `recommendation` | 0 → None | **OBSERVED** (Live Nice = 0) |
| 2026-10-21T12:58:06Z | 404 | `(udev-worker)` | `recommend` | `recommendation` | 0 → None | **OBSERVED** (Live Nice = 0) |
| 2026-10-21T12:58:06Z | 405 | `(udev-worker)` | `recommend` | `recommendation` | 0 → None | **OBSERVED** (Live Nice = 0) |
| 2026-10-21T12:58:06Z | 406 | `(udev-worker)` | `recommend` | `recommendation` | 0 → None | **OBSERVED** (Live Nice = 0) |
| 2026-10-21T12:58:06Z | 407 | `sh` | `recommend` | `recommendation` | 0 → None | **OBSERVED** (Live Nice = 0) |
| 2026-10-21T12:58:06Z | 408 | `make` | `recommend` | `recommendation` | 0 → None | **OBSERVED** (Live Nice = 0) |
| 2026-10-21T12:58:06Z | 412 | `bash` | `recommend` | `recommendation` | 0 → None | **OBSERVED** (Live Nice = 0) |
| 2026-10-21T12:58:06Z | 440 | `monitor` | `recommend` | `recommendation` | 0 → None | **OBSERVED** (Live Nice = 0) |

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

