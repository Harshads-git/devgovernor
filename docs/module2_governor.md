# Module 2 — Scheduling Priority Governor

> **Student 2 | Branch: `student2-governor-rules`**  
> File: `module2_governor/governor.c`  
> Language: C99 / POSIX 2008  
> Compiled with: `gcc -Wall -Wextra -O2 -std=c99 -D_POSIX_C_SOURCE=200809L`

---

## 1. Purpose & Scope

The **Scheduling Priority Governor** is a command-line tool that manages Linux
process scheduling priorities using the standard POSIX/Linux APIs
`getpriority(2)` and `setpriority(2)`. It operates entirely in **user space**
— it does not modify kernel source code or implement a custom in-kernel
scheduler. Instead, it leverages the existing Linux **Completely Fair Scheduler
(CFS)** nice-value mechanism to steer CPU allocation toward developer
workloads.

---

## 2. CLI Reference

### 2.1 `inspect <PID>`

```bash
./module2_governor/governor inspect <PID> [--history <file>]
```

Reads `/proc/<PID>/status` (name, state, PPID, VmRSS), calls `getpriority()`
to retrieve the current nice value, looks it up in the kernel
`sched_prio_to_weight` table, and prints:

| Field | Source |
|---|---|
| PID, Name, PPID | `/proc/<PID>/status` |
| State | `/proc/<PID>/status` (R/S/D/Z/T/I) |
| Resident Memory (MB) | VmRSS field converted from kB |
| Current Nice | `getpriority(PRIO_PROCESS, pid)` |
| Priority Class | Derived from nice range |
| CFS Weight | Kernel `sched_prio_to_weight` table |

### 2.2 `evaluate <PID> <TARGET_NICE>` (Dry-Run)

```bash
./module2_governor/governor evaluate <PID> <TARGET_NICE> [--history <file>]
```

Performs a **dry-run analysis** — no process is modified. Outputs:
- Current vs. proposed nice values and CFS weights
- Weight delta as a percentage change in CPU share
- Direction of change (boost vs. throttle) and privilege requirements
- Recommendation logged to audit trail with status `recommendation`

### 2.3 `set-nice <PID> <TARGET_NICE> [--yes]`

```bash
./module2_governor/governor set-nice <PID> <TARGET_NICE> [--yes | -y] [--history <file>]
```

Modifies the process nice value via `setpriority()`. Workflow:

1. Applies all **safety guards** (PID 1, self-PID, range check)
2. Reads current state from `/proc`
3. Prints warning / privilege advisory
4. Prompts `Do you want to apply this priority change? [y/N]`  
   _(skipped with `--yes` / `-y` for non-interactive scripts)_
5. Calls `setpriority(PRIO_PROCESS, pid, target_nice)`
6. **Verifies** result with `getpriority()` — logs the actual resulting nice

### 2.4 `recommend-all --input <processes.json>`

```bash
./module2_governor/governor recommend-all --input data/processes.json [--history <file>]
```

Reads the JSON snapshot produced by Module 1 and applies the governance policy
to every process entry:

| `workload_category` | Current Nice | Action | Recommended Nice |
|---|---|---|---|
| `build_process` / `development_tool` | ≥ 0 | **boost** | −2 (needs root) |
| `build_process` / `development_tool` | < 0 | no_change | — |
| `background_process` | < 10 | **throttle** | 10 (unprivileged) |
| `background_process` | ≥ 10 | no_change | — |
| `development_server` | ≠ 0 | **baseline** | 0 |
| `development_server` | 0 | no_change | — |
| `system_or_unknown` | any | no_change | — |

---

## 3. Safety Guards

Every mutating command (`set-nice`) and analytical command (`evaluate`) runs
through these guards **before** any operation:

| Guard | Rationale |
|---|---|
| `pid <= 1` rejected | PID 1 is `init`/`systemd`, root of process tree; touching it risks system instability |
| `pid == getpid()` rejected | Altering the governor's own priority could corrupt its audit trail or scheduling |
| `nice < -20 \|\| nice > 19` rejected | Linux kernel enforces this range; invalid values would cause `EINVAL` |
| Post-change `getpriority()` verify | Confirms the kernel applied the change; detects silent clamping or races |

---

## 4. Audit Trail (`data/actions.jsonl`)

Every operation appends one **JSON-Lines** record:

```jsonl
{"timestamp": "2026-10-15T12:00:00Z", "pid": 1105, "process_name": "gcc",
 "previous_nice": 0, "requested_nice": -2, "resulting_nice": -2,
 "action_type": "set_nice", "status": "success",
 "message": "Changed nice from 0 to -2 (getpriority verified: -2)"}
```

| Field | Type | Description |
|---|---|---|
| `timestamp` | string | ISO-8601 UTC |
| `pid` | int | Target process ID |
| `process_name` | string | Name from `/proc` |
| `previous_nice` | int \| null | Nice before operation |
| `requested_nice` | int \| null | Target nice value (null for inspect) |
| `resulting_nice` | int \| null | Verified post-operation nice |
| `action_type` | string | `inspect`, `evaluate`, `set_nice`, `recommend` |
| `status` | string | `success`, `recommendation`, `rejected`, `failed`, `aborted`, `noop` |
| `message` | string | Human-readable explanation |

---

## 5. OS Concepts & Viva Answers

### Q1: What system calls are used?

- **`getpriority(PRIO_PROCESS, pid)`** — reads the current nice value.
- **`setpriority(PRIO_PROCESS, pid, nice)`** — sets a new nice value.
- Both are defined in `<sys/resource.h>` and POSIX 2008.

### Q2: Why must `errno` be cleared before `getpriority()`?

`getpriority()` returns `−1` both on error **and** when the nice value is
legitimately `−1`. To tell them apart, the code clears `errno = 0` before the
call. If `errno != 0` after a return of `−1`, an actual error occurred. This
is documented in `man 2 getpriority` and the governor code contains a
`[VIVA]` comment at this exact line.

### Q3: How does CFS use nice values?

The kernel converts nice values to integer weights using the fixed
`sched_prio_to_weight[]` table (implemented verbatim in `governor.c`).
The weights are chosen so that each nice step changes CPU share by ~25%.
A task's *virtual runtime* (`vruntime`) is incremented at rate
`delta_exec × NICE_0_LOAD / weight`.  Tasks with **higher weight** accrue
`vruntime` more slowly and are therefore selected more often by the CFS
red-black tree — which always picks the process with the smallest `vruntime`.

### Q4: What Linux capability controls nice adjustments?

**`CAP_SYS_NICE`** (defined in `<sys/capability.h>`). Without it (i.e., as a
non-root user):
- Raising a nice value (e.g., 0 → 10) is always permitted for own processes.
- Lowering a nice value (e.g., 0 → −5) returns `EPERM`.

The governor explicitly checks for `EPERM`/`EACCES` after `setpriority()` and
prints a hint suggesting `sudo` or `CAP_SYS_NICE`.

### Q5: Why is PID 1 protected?

PID 1 (`init`/`systemd`) is the **ancestor of all user-space processes** and
handles critical system events (service supervision, signal routing, zombie
reaping). Deprioritising or destabilising PID 1 could cause unresponsive
hardware event handling or kernel panics.

### Q6: What is ESRCH and when does it appear?

`ESRCH` ("No such process") is returned by both `getpriority()` and
`setpriority()` when the target PID has exited between our existence check
(reading `/proc`) and the actual system call. This is a **Time-of-Check to
Time-of-Use (TOCTOU) race**, unavoidable on a live system. The governor
handles it gracefully and logs a `failed` record.

---

## 6. Build & Test

```bash
# Build governor binary
make -C module2_governor/

# Smoke tests (safety guards + recommend-all on sample data)
make -C module2_governor/ check

# Full Module 2 test suite (15 tests)
python3 tests/test_governor.py -v
```

Zero compiler warnings with `-Wall -Wextra -O2 -std=c99`.
