/**
 * ============================================================================
 * DevGovernor — Scheduling Priority Governor (Module 2)
 * ============================================================================
 * Student 2: Resource Management and Scheduling Governor
 *
 * Course: Operating Systems Lab (B.Tech IT)
 * Branch: student2-governor-rules
 *
 * Purpose:
 *   Manages Linux process scheduling priorities via the standard POSIX / Linux
 *   APIs (getpriority, setpriority).  Reads process snapshots written by
 *   Module 1 and emits policy-based recommendations to steer the Linux CFS
 *   scheduler toward developer-workload priorities.
 *
 * Core Capabilities:
 *   1. inspect      <PID>                      Inspect process state, resident
 *                                              memory, nice value, and CFS class.
 *   2. evaluate     <PID> <TARGET_NICE>        Dry-run evaluation & policy
 *                                              recommendation (no modification).
 *   3. set-nice     <PID> <TARGET_NICE>        Validated nice adjustment with
 *                                              interactive confirmation (or --yes).
 *   4. recommend-all --input <file>            Policy engine: reads Module 1's
 *                                              processes.json and emits per-
 *                                              process recommendations.
 *   5. JSON-Lines Logging                      Appends audit trail to
 *                                              data/actions.jsonl.
 *
 * Safety & OS Principles:
 *   - Never modifies PID 1 (init/systemd) or self PID.
 *   - Strictly validates nice value range [-20, 19].
 *   - Differentiates between raising nice (lower CPU priority, unprivileged)
 *     and lowering nice (higher CPU priority, requires CAP_SYS_NICE or root).
 *   - Safe error handling: handles ESRCH (process vanished) and EACCES/EPERM.
 *   - Verifies post-change: calls getpriority() after setpriority() to confirm
 *     the kernel actually applied the requested change.
 *   - No automatic/uncontrolled background modifications.
 *
 * Compilation:
 *   gcc -Wall -Wextra -O2 -std=c99 -D_POSIX_C_SOURCE=200809L -o governor governor.c
 *
 * Viva Notes (inline throughout code):
 *   [VIVA] markers explain the OS theory behind each design decision.
 * ============================================================================
 */

/* _GNU_SOURCE enables gmtime_r and other GNU/POSIX extensions */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <ctype.h>
#include <sys/resource.h>  /* getpriority(), setpriority(), PRIO_PROCESS */
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>

/* ============================================================
 * Constants
 * ============================================================ */
#define DEFAULT_AUDIT_FILE   "data/actions.jsonl"
#define MAX_NAME_LEN         256
#define MAX_PATH_LEN         512
#define MAX_MSG_LEN          512
#define MAX_LINE_LEN         4096  /* For JSON line parsing */

/* Sentinel: a requested_nice value that signals "no request" (inspect-only) */
#define NO_REQUESTED_NICE    -999

/* ============================================================
 * CFS Nice-to-Weight Table
 * [VIVA] The Linux CFS scheduler converts nice values into integer weights
 * using a fixed table (sched_prio_to_weight[] in kernel/sched/core.c).
 * The ratio between adjacent entries is approximately 1.25, so each nice
 * step changes CPU share by ~25%.  A task with weight W_i and total runqueue
 * weight W_total receives:
 *   CPU share ~ W_i / W_total
 * Virtual runtime (vruntime) is incremented proportionally to 1/weight, so
 * higher-weight tasks accumulate vruntime more slowly and are picked more often.
 * ============================================================ */
static const unsigned int cfs_nice_to_weight[40] = {
 /* nice -20 */ 88761,
 /* nice -19 */ 71755,
 /* nice -18 */ 56483,
 /* nice -17 */ 46273,
 /* nice -16 */ 36291,
 /* nice -15 */ 29154,
 /* nice -14 */ 23254,
 /* nice -13 */ 18705,
 /* nice -12 */ 14949,
 /* nice -11 */ 11916,
 /* nice -10 */  9548,
 /* nice  -9 */  7620,
 /* nice  -8 */  6100,
 /* nice  -7 */  4904,
 /* nice  -6 */  3906,
 /* nice  -5 */  3121,
 /* nice  -4 */  2501,
 /* nice  -3 */  1991,
 /* nice  -2 */  1586,
 /* nice  -1 */  1277,
 /* nice   0 */  1024,  /* default weight -- baseline */
 /* nice  +1 */   820,
 /* nice  +2 */   655,
 /* nice  +3 */   526,
 /* nice  +4 */   423,
 /* nice  +5 */   335,
 /* nice  +6 */   272,
 /* nice  +7 */   215,
 /* nice  +8 */   172,
 /* nice  +9 */   137,
 /* nice +10 */   110,
 /* nice +11 */    87,
 /* nice +12 */    70,
 /* nice +13 */    56,
 /* nice +14 */    45,
 /* nice +15 */    36,
 /* nice +16 */    29,
 /* nice +17 */    23,
 /* nice +18 */    18,
 /* nice +19 */    15,
};

/* Helper: return CFS weight for a nice value in [-20, 19] */
static unsigned int nice_to_cfs_weight(int nice) {
    if (nice < -20) nice = -20;
    if (nice >  19) nice =  19;
    return cfs_nice_to_weight[nice + 20];
}

/* ============================================================
 * /proc Parsing Helpers
 * ============================================================ */

/*
 * get_process_info -- Read process metadata from /proc/<pid>/status
 *
 * Populates: name_out, state_out, ppid_out, mem_mb_out.
 * Returns 0 on success, -1 if the process doesn't exist or /proc is
 * inaccessible.
 *
 * [VIVA] /proc is a virtual pseudo-filesystem maintained entirely in kernel
 * memory; no disk I/O occurs when reading these files.  Each numeric
 * subdirectory corresponds to a live process identified by its PID.
 */
static int get_process_info(int pid, char *name_out, size_t name_len,
                            char *state_out, size_t state_len,
                            int *ppid_out, double *mem_mb_out) {
    (void)state_len;  /* suppress unused-parameter warning */

    char path[MAX_PATH_LEN];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);

    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char line[256];
    name_out[0]  = '\0';
    state_out[0] = '\0';
    *ppid_out    = 0;
    *mem_mb_out  = 0.0;

    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "Name:", 5) == 0) {
            char *p = line + 5;
            while (*p == ' ' || *p == '\t') p++;
            char *end = p + strlen(p) - 1;
            while (end > p && (*end == '\n' || *end == '\r' || *end == ' '))
                *end-- = '\0';
            snprintf(name_out, name_len, "%s", p);

        } else if (strncmp(line, "State:", 6) == 0) {
            char *p = line + 6;
            while (*p == ' ' || *p == '\t') p++;
            if (*p) { state_out[0] = *p; state_out[1] = '\0'; }

        } else if (strncmp(line, "PPid:", 5) == 0) {
            *ppid_out = atoi(line + 5);

        } else if (strncmp(line, "VmRSS:", 6) == 0) {
            long kb = 0;
            if (sscanf(line + 6, "%ld", &kb) == 1)
                *mem_mb_out = (double)kb / 1024.0;
        }
    }

    fclose(f);
    return 0;
}

/*
 * get_process_nice -- Read current nice value via getpriority()
 *
 * [VIVA] getpriority(PRIO_PROCESS, pid) returns the nice value on success.
 * CRITICALLY: -1 is BOTH a valid nice value AND the error return.  To
 * distinguish between them, errno must be cleared to 0 before the call;
 * if errno is non-zero after the call returns -1, an error occurred.
 */
static int get_process_nice(int pid, int *nice_out) {
    errno = 0;                              /* [VIVA] Must clear errno first */
    int val = getpriority(PRIO_PROCESS, (id_t)pid);
    if (errno != 0) return -1;             /* real error (ESRCH, EPERM, ...) */
    *nice_out = val;
    return 0;
}

/* ============================================================
 * Audit Logging
 * ============================================================ */

/*
 * log_action -- Append a JSON-Lines record to the audit trail.
 *
 * Fields: timestamp, pid, process_name, previous_nice, requested_nice,
 * resulting_nice, action_type, status, message.
 *
 * requested_nice == NO_REQUESTED_NICE -> field emitted as JSON null.
 * has_result == 0                     -> resulting_nice emitted as null.
 */
static void log_action(const char *audit_path, int pid, const char *process_name,
                       int prev_nice, int req_nice,
                       int result_nice, int has_result,
                       const char *action_type,
                       const char *status, const char *message) {
    FILE *f = fopen(audit_path, "a");
    if (!f) {
        fprintf(stderr, "[Governor Warning] Cannot open audit file '%s': %s\n",
                audit_path, strerror(errno));
        return;
    }

    time_t now = time(NULL);
    struct tm tm_utc;
    gmtime_r(&now, &tm_utc);
    char ts[64];
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);

    /* Sanitise message (replace " with ') for valid JSON */
    char safe_msg[MAX_MSG_LEN];
    {
        size_t i = 0, j = 0;
        const char *m = message ? message : "";
        while (m[i] && j < sizeof(safe_msg) - 1) {
            safe_msg[j++] = (m[i] == '"') ? '\'' : m[i];
            i++;
        }
        safe_msg[j] = '\0';
    }

    fprintf(f, "{\"timestamp\": \"%s\", \"pid\": %d, \"process_name\": \"%s\", ",
            ts, pid, process_name ? process_name : "unknown");

    if (prev_nice >= -20 && prev_nice <= 19)
        fprintf(f, "\"previous_nice\": %d, ", prev_nice);
    else
        fprintf(f, "\"previous_nice\": null, ");

    if (req_nice >= -20 && req_nice <= 19)
        fprintf(f, "\"requested_nice\": %d, ", req_nice);
    else
        fprintf(f, "\"requested_nice\": null, ");

    if (has_result && result_nice >= -20 && result_nice <= 19)
        fprintf(f, "\"resulting_nice\": %d, ", result_nice);
    else
        fprintf(f, "\"resulting_nice\": null, ");

    fprintf(f, "\"action_type\": \"%s\", \"status\": \"%s\", \"message\": \"%s\"}\n",
            action_type, status, safe_msg);

    fclose(f);
}

/* ============================================================
 * Priority Class Helper
 * ============================================================ */
static const char *priority_class_label(int nice) {
    if (nice < -10) return "REALTIME-ADJACENT (very high weight, nice < -10)";
    if (nice <   0) return "HIGH PRIORITY (nice < 0, boosted CPU share)";
    if (nice ==  0) return "STANDARD PRIORITY (nice 0, default Linux baseline)";
    if (nice <= 10) return "LOW / BACKGROUND (nice 1-10, reduced CPU share)";
    return "BATCH / IDLE (nice 11-19, minimal CPU share)";
}

/* ============================================================
 * Command: inspect <PID>
 * ============================================================ */
static int cmd_inspect(int pid, const char *audit_path) {
    char name[MAX_NAME_LEN] = "unknown";
    char state[16]          = "?";
    int  ppid               = 0;
    double mem_mb           = 0.0;
    int  current_nice       = 0;

    if (get_process_info(pid, name, sizeof(name), state, sizeof(state),
                         &ppid, &mem_mb) != 0) {
        fprintf(stderr,
                "Error: PID %d not found or inaccessible "
                "(check /proc/%d/status).\n", pid, pid);
        return 1;
    }

    if (get_process_nice(pid, &current_nice) != 0) {
        fprintf(stderr, "Error reading nice value for PID %d: %s\n",
                pid, strerror(errno));
        return 1;
    }

    unsigned int weight = nice_to_cfs_weight(current_nice);

    const char *state_desc = "?";
    switch (state[0]) {
        case 'R': state_desc = "Running";                  break;
        case 'S': state_desc = "Sleeping (interruptible)"; break;
        case 'D': state_desc = "Waiting (uninterruptible)";break;
        case 'Z': state_desc = "Zombie";                   break;
        case 'T': state_desc = "Stopped";                  break;
        case 'I': state_desc = "Idle (kernel thread)";     break;
    }

    printf("\n");
    printf("=== DevGovernor: Process Inspection ===\n");
    printf("  PID            : %d\n",      pid);
    printf("  Name           : %s\n",      name);
    printf("  PPID           : %d\n",      ppid);
    printf("  State          : %s (%s)\n", state, state_desc);
    printf("  Resident Mem   : %.2f MB\n", mem_mb);
    printf("\n");
    printf("  -- CFS Scheduling Context --\n");
    printf("  Current Nice   : %d\n",      current_nice);
    printf("  Priority Class : %s\n",      priority_class_label(current_nice));
    printf("  CFS Weight     : %u  (kernel sched_prio_to_weight table)\n", weight);
    printf("  Valid Range    : -20 (highest CPU priority) to 19 (lowest)\n");
    printf("\n");
    printf("  Note: CAP_SYS_NICE or root required to LOWER nice (boost CPU).\n");
    printf("        Any user may RAISE nice (throttle) for their own processes.\n\n");

    log_action(audit_path, pid, name, current_nice, NO_REQUESTED_NICE,
               current_nice, 1, "inspect", "success",
               "Inspected process scheduling priority and state");
    return 0;
}

/* ============================================================
 * Command: evaluate <PID> <TARGET_NICE>  (dry-run)
 * ============================================================ */
static int cmd_evaluate(int pid, int target_nice, const char *audit_path) {
    /* Safety Guard 1: PID 1 */
    if (pid <= 1) {
        fprintf(stderr,
                "Evaluation Rejected: PID %d is reserved (init/systemd).\n"
                "  [OS] PID 1 is the root of the process tree; modifying its\n"
                "       priority could destabilise the entire system.\n", pid);
        log_action(audit_path, pid, "init/root", NO_REQUESTED_NICE, target_nice,
                   NO_REQUESTED_NICE, 0, "evaluate", "rejected",
                   "Target PID <= 1: init/systemd is protected");
        return 1;
    }

    /* Safety Guard 2: Self-PID */
    if (pid == (int)getpid()) {
        fprintf(stderr,
                "Evaluation Rejected: PID %d is the governor's own PID.\n", pid);
        log_action(audit_path, pid, "devgovernor", NO_REQUESTED_NICE, target_nice,
                   NO_REQUESTED_NICE, 0, "evaluate", "rejected",
                   "Target PID is the governor itself");
        return 1;
    }

    /* Safety Guard 3: Nice range */
    if (target_nice < -20 || target_nice > 19) {
        fprintf(stderr,
                "Evaluation Rejected: Nice value %d is outside valid range "
                "[-20, 19].\n", target_nice);
        log_action(audit_path, pid, "unknown", NO_REQUESTED_NICE, target_nice,
                   NO_REQUESTED_NICE, 0, "evaluate", "rejected",
                   "Nice value out of valid range [-20, 19]");
        return 1;
    }

    char name[MAX_NAME_LEN] = "unknown";
    char state[16]          = "?";
    int  ppid               = 0;
    double mem_mb           = 0.0;
    int  current_nice       = 0;

    if (get_process_info(pid, name, sizeof(name), state, sizeof(state),
                         &ppid, &mem_mb) != 0) {
        fprintf(stderr,
                "Evaluation Failed: Process PID %d not found in /proc.\n"
                "  [OS] ESRCH -- process may have exited between check and call.\n",
                pid);
        log_action(audit_path, pid, "unknown", NO_REQUESTED_NICE, target_nice,
                   NO_REQUESTED_NICE, 0, "evaluate", "failed",
                   "Process not found (ESRCH)");
        return 1;
    }

    if (get_process_nice(pid, &current_nice) != 0) {
        fprintf(stderr, "Error inspecting PID %d nice value: %s\n",
                pid, strerror(errno));
        return 1;
    }

    unsigned int cur_weight = nice_to_cfs_weight(current_nice);
    unsigned int tgt_weight = nice_to_cfs_weight(target_nice);
    int          delta      = target_nice - current_nice;

    printf("\n");
    printf("=== DevGovernor: Priority Evaluation (Dry-Run) ===\n");
    printf("  PID            : %d (%s)\n", pid, name);
    printf("  Current Nice   : %d  (CFS weight: %u)\n", current_nice, cur_weight);
    printf("  Proposed Nice  : %d  (CFS weight: %u)\n", target_nice,  tgt_weight);
    printf("  Nice Delta     : %+d\n", delta);
    printf("\n");

    char msg[MAX_MSG_LEN];

    if (delta == 0) {
        printf("  Recommendation : NO CHANGE\n");
        printf("  Reason         : Proposed nice matches current value.\n");
        printf("  CFS Effect     : No change in vruntime accrual rate.\n");
        snprintf(msg, sizeof(msg),
                 "No change needed: current nice %d already equals target",
                 current_nice);

    } else if (delta < 0) {
        /* Lowering nice = raising CPU priority -- requires privilege */
        printf("  Recommendation : BOOST PRIORITY (nice %d -> %d)\n",
               current_nice, target_nice);
        printf("  CFS Effect     : Weight %u -> %u (+%.1f%% CPU share)\n",
               cur_weight, tgt_weight,
               100.0 * ((double)tgt_weight / cur_weight - 1.0));
        printf("  CFS Mechanism  : Lower nice => higher sched weight => vruntime\n");
        printf("                   accrues more slowly => scheduler picks process\n");
        printf("                   more frequently => more CPU time.\n");
        printf("\n");
        printf("  WARNING: Lowering nice requires CAP_SYS_NICE or sudo.\n");
        printf("           EPERM will be returned for unprivileged callers.\n");
        snprintf(msg, sizeof(msg),
                 "Recommend boost from %d to %d "
                 "(CFS weight %u->%u; requires CAP_SYS_NICE/root)",
                 current_nice, target_nice, cur_weight, tgt_weight);

    } else {
        /* Raising nice = lowering CPU priority -- unprivileged ok */
        printf("  Recommendation : THROTTLE / BACKGROUND (nice %d -> %d)\n",
               current_nice, target_nice);
        printf("  CFS Effect     : Weight %u -> %u (-%.1f%% CPU share)\n",
               cur_weight, tgt_weight,
               100.0 * (1.0 - (double)tgt_weight / cur_weight));
        printf("  CFS Mechanism  : Higher nice => lower sched weight => vruntime\n");
        printf("                   accrues faster => process is picked less often\n");
        printf("                   => CPU timeslices freed for foreground tasks.\n");
        printf("\n");
        printf("  Privilege      : Raising nice is unprivileged for own processes.\n");
        snprintf(msg, sizeof(msg),
                 "Recommend throttle from %d to %d "
                 "(CFS weight %u->%u; unprivileged ok)",
                 current_nice, target_nice, cur_weight, tgt_weight);
    }

    printf("\n  DRY-RUN: No changes applied to PID %d.\n\n", pid);

    log_action(audit_path, pid, name, current_nice, target_nice, current_nice, 1,
               "evaluate", "recommendation", msg);
    return 0;
}

/* ============================================================
 * Command: set-nice <PID> <TARGET_NICE> [--yes]
 * ============================================================ */
static int cmd_set_nice(int pid, int target_nice, int auto_yes,
                        const char *audit_path) {
    /* Safety Guard 1: PID 1 */
    if (pid <= 1) {
        fprintf(stderr,
                "Modification Blocked: PID %d is init/systemd (protected).\n",
                pid);
        log_action(audit_path, pid, "init/root", NO_REQUESTED_NICE, target_nice,
                   NO_REQUESTED_NICE, 0, "set_nice", "rejected",
                   "Modification of PID <= 1 blocked");
        return 1;
    }

    /* Safety Guard 2: Self-PID */
    if (pid == (int)getpid()) {
        fprintf(stderr, "Modification Blocked: Cannot alter governor's own PID.\n");
        log_action(audit_path, pid, "devgovernor", NO_REQUESTED_NICE, target_nice,
                   NO_REQUESTED_NICE, 0, "set_nice", "rejected",
                   "Modification of governor self-PID blocked");
        return 1;
    }

    /* Safety Guard 3: Range */
    if (target_nice < -20 || target_nice > 19) {
        fprintf(stderr,
                "Error: Nice value %d is outside valid range [-20, 19].\n",
                target_nice);
        log_action(audit_path, pid, "unknown", NO_REQUESTED_NICE, target_nice,
                   NO_REQUESTED_NICE, 0, "set_nice", "rejected",
                   "Target nice value outside [-20, 19]");
        return 1;
    }

    char name[MAX_NAME_LEN] = "unknown";
    char state[16]          = "?";
    int  ppid               = 0;
    double mem_mb           = 0.0;
    int  current_nice       = 0;

    if (get_process_info(pid, name, sizeof(name), state, sizeof(state),
                         &ppid, &mem_mb) != 0) {
        fprintf(stderr, "Error: PID %d not found or exited (ESRCH).\n", pid);
        log_action(audit_path, pid, "unknown", NO_REQUESTED_NICE, target_nice,
                   NO_REQUESTED_NICE, 0, "set_nice", "failed",
                   "Process not found in /proc (ESRCH)");
        return 1;
    }

    if (get_process_nice(pid, &current_nice) != 0) {
        fprintf(stderr, "Error: Cannot read nice for PID %d: %s\n",
                pid, strerror(errno));
        return 1;
    }

    /* No-op fast path */
    if (current_nice == target_nice) {
        printf("Notice: PID %d (%s) already at nice %d. No action taken.\n",
               pid, name, current_nice);
        log_action(audit_path, pid, name, current_nice, target_nice,
                   current_nice, 1, "set_nice", "noop",
                   "Target nice equals current nice; no change needed");
        return 0;
    }

    printf("\n");
    printf("=== DevGovernor: Priority Modification ===\n");
    printf("  PID            : %d (%s)\n", pid, name);
    printf("  Current Nice   : %d\n",      current_nice);
    printf("  Requested Nice : %d\n",      target_nice);

    if (target_nice < current_nice) {
        printf("\n  WARNING: LOWERING nice (raising CPU priority) requires\n");
        printf("           CAP_SYS_NICE or superuser (sudo). Without it,\n");
        printf("           setpriority() will return EPERM.\n");
    } else {
        printf("\n  INFO: RAISING nice (lowering CPU priority). Frees CPU\n");
        printf("        timeslices for higher-priority dev tasks. Unprivileged\n");
        printf("        users may do this for their own processes.\n");
    }

    /* Interactive confirmation */
    if (!auto_yes) {
        printf("\n  Do you want to apply this priority change? [y/N]: ");
        fflush(stdout);
        char buf[16] = {0};
        if (fgets(buf, sizeof(buf), stdin) == NULL ||
            (buf[0] != 'y' && buf[0] != 'Y')) {
            printf("  Aborted by user. No changes made.\n\n");
            log_action(audit_path, pid, name, current_nice, target_nice,
                       current_nice, 1, "set_nice", "aborted",
                       "User declined interactive confirmation");
            return 0;
        }
    } else {
        printf("  [--yes flag detected] Skipping interactive confirmation.\n");
    }

    /*
     * Apply setpriority()
     * [VIVA] setpriority(PRIO_PROCESS, pid, nice) instructs the kernel CFS
     * to update the process's nice value.  The kernel checks:
     *   (a) target PID exists -> ESRCH on failure
     *   (b) caller has permission -> EPERM if lowering nice without privilege
     *   (c) value is in range -> EINVAL
     */
    errno = 0;
    int ret = setpriority(PRIO_PROCESS, (id_t)pid, target_nice);

    if (ret != 0) {
        char err_msg[MAX_MSG_LEN];
        snprintf(err_msg, sizeof(err_msg),
                 "setpriority() failed: %s (errno %d)", strerror(errno), errno);
        fprintf(stderr, "\n  Error: %s\n", err_msg);

        if (errno == EPERM || errno == EACCES) {
            fprintf(stderr,
                    "  Hint: Retry with: sudo ./governor set-nice %d %d --yes\n",
                    pid, target_nice);
        } else if (errno == ESRCH) {
            fprintf(stderr,
                    "  Hint: Process %d exited between our read and write (TOCTOU).\n",
                    pid);
        }

        log_action(audit_path, pid, name, current_nice, target_nice,
                   current_nice, 1, "set_nice", "failed", err_msg);
        return 1;
    }

    /*
     * Post-change verification via getpriority()
     * [VIVA] Always verify because:
     *   1. Kernel may silently clamp the value in edge cases.
     *   2. A concurrent privileged process could change nice between set/verify.
     */
    int verified_nice = target_nice;
    if (get_process_nice(pid, &verified_nice) != 0) {
        printf("\n  Notice: setpriority() succeeded but post-verify failed.\n");
        printf("  Process may have exited. Audit logged with requested value.\n\n");
        log_action(audit_path, pid, name, current_nice, target_nice,
                   target_nice, 1, "set_nice", "success",
                   "Updated nice (post-verify failed; process may have exited)");
        return 0;
    }

    printf("\n  Success: PID %d (%s) nice %d -> %d"
           " (verified by getpriority())\n\n",
           pid, name, current_nice, verified_nice);

    char success_msg[MAX_MSG_LEN];
    snprintf(success_msg, sizeof(success_msg),
             "Changed nice from %d to %d (getpriority verified: %d)",
             current_nice, target_nice, verified_nice);
    log_action(audit_path, pid, name, current_nice, target_nice,
               verified_nice, 1, "set_nice", "success", success_msg);
    return 0;
}

/* ============================================================
 * Minimal JSON helpers (no external library)
 * Parses the flat JSON objects emitted by Module 1's processes.json.
 * ============================================================ */

static const char *json_find_key(const char *haystack, const char *key) {
    char needle[128];
    snprintf(needle, sizeof(needle), "\"%s\":", key);
    return strstr(haystack, needle);
}

static int json_get_int(const char *obj, const char *key, int *out) {
    const char *p = json_find_key(obj, key);
    if (!p) return 0;
    p += strlen(key) + 3;  /* skip "key": */
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\0') return 0;
    char *endptr = NULL;
    long v = strtol(p, &endptr, 10);
    if (endptr == p) return 0;
    *out = (int)v;
    return 1;
}

static int json_get_str(const char *obj, const char *key,
                        char *out, size_t out_len) {
    const char *p = json_find_key(obj, key);
    if (!p) return 0;
    p += strlen(key) + 3;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '"') return 0;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i < out_len - 1)
        out[i++] = *p++;
    out[i] = '\0';
    return 1;
}

/* ============================================================
 * Recommendation Engine
 * ============================================================ */

typedef struct {
    int  pid;
    char name[MAX_NAME_LEN];
    char category[64];
    int  nice;
    int  recommended_nice;
    char action[32];       /* "boost", "throttle", "baseline", "no_change" */
    char rationale[MAX_MSG_LEN];
    int  needs_root;
} ProcessRec;

/*
 * apply_policy -- map workload_category + nice -> governance recommendation
 *
 * Policy rules (per project spec):
 *   build_process | development_tool AND nice >= 0 -> BOOST to nice -2
 *   background_process AND nice < 10              -> THROTTLE to nice 10
 *   development_server                             -> BASELINE at nice 0
 *   system_or_unknown | already optimal            -> NO_CHANGE
 */
static void apply_policy(ProcessRec *r) {
    r->needs_root = 0;

    if (strcmp(r->category, "build_process") == 0 ||
        strcmp(r->category, "development_tool") == 0) {

        if (r->nice >= 0) {
            r->recommended_nice = -2;
            r->needs_root       = 1;
            snprintf(r->action, sizeof(r->action), "boost");
            snprintf(r->rationale, sizeof(r->rationale),
                     "Build/dev tool at nice %d; recommend nice -2 to accelerate "
                     "compilation/indexing (CAP_SYS_NICE required)", r->nice);
        } else {
            r->recommended_nice = r->nice;
            snprintf(r->action, sizeof(r->action), "no_change");
            snprintf(r->rationale, sizeof(r->rationale),
                     "Build/dev tool already at elevated nice %d; no change needed",
                     r->nice);
        }

    } else if (strcmp(r->category, "background_process") == 0) {

        if (r->nice < 10) {
            r->recommended_nice = 10;
            snprintf(r->action, sizeof(r->action), "throttle");
            snprintf(r->rationale, sizeof(r->rationale),
                     "Background process at nice %d; recommend nice 10 to yield "
                     "CPU timeslices to active dev tasks (unprivileged ok)", r->nice);
        } else {
            r->recommended_nice = r->nice;
            snprintf(r->action, sizeof(r->action), "no_change");
            snprintf(r->rationale, sizeof(r->rationale),
                     "Background process sufficiently throttled (nice %d >= 10)",
                     r->nice);
        }

    } else if (strcmp(r->category, "development_server") == 0) {

        if (r->nice == 0) {
            r->recommended_nice = 0;
            snprintf(r->action, sizeof(r->action), "no_change");
            snprintf(r->rationale, sizeof(r->rationale),
                     "Dev server at neutral baseline nice 0; optimal");
        } else {
            r->recommended_nice = 0;
            r->needs_root       = (r->nice < 0) ? 1 : 0;
            snprintf(r->action, sizeof(r->action), "baseline");
            snprintf(r->rationale, sizeof(r->rationale),
                     "Dev server at nice %d; recommend baseline nice 0 for "
                     "consistent request-response latency%s", r->nice,
                     (r->nice < 0) ? " (needs CAP_SYS_NICE to raise)" : "");
        }

    } else {
        /* system_or_unknown -- do not touch */
        r->recommended_nice = r->nice;
        snprintf(r->action, sizeof(r->action), "no_change");
        snprintf(r->rationale, sizeof(r->rationale),
                 "Category '%s': governor policy does not govern this class",
                 r->category);
    }
}

/* ============================================================
 * Command: recommend-all --input <processes.json>
 * ============================================================ */
static int cmd_recommend_all(const char *input_path, const char *audit_path) {
    FILE *f = fopen(input_path, "r");
    if (!f) {
        fprintf(stderr,
                "Error: Cannot open '%s': %s\n"
                "  (Run Module 1 first: "
                "./module1_monitor/monitor --output %s --oneshot)\n",
                input_path, strerror(errno), input_path);
        return 1;
    }

    printf("\n");
    printf("=== DevGovernor: Workload-Aware Recommendation Engine ===\n");
    printf("    Input: %s\n\n", input_path);

    /*
     * Streaming JSON object parser:
     * We accumulate text between '{' and matching '}' belonging to each
     * process entry inside the "processes" array.
     */
    char obj_buf[MAX_LINE_LEN * 8];
    int  obj_buf_len      = 0;
    int  in_processes     = 0;
    int  brace_depth      = 0;
    int  top_brace_seen   = 0;
    int  rec_count        = 0;
    int  boost_count      = 0;
    int  throttle_count   = 0;
    int  baseline_count   = 0;
    int  nochange_count   = 0;

    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), f)) {
        if (!in_processes) {
            if (strstr(line, "\"processes\"")) in_processes = 1;
            continue;
        }

        for (int ci = 0; line[ci] != '\0'; ci++) {
            char c = line[ci];

            if (c == '{') {
                if (!top_brace_seen) top_brace_seen = 1;
                brace_depth++;
            }

            if (top_brace_seen && obj_buf_len < (int)sizeof(obj_buf) - 1)
                obj_buf[obj_buf_len++] = c;

            if (c == '}' && top_brace_seen) {
                brace_depth--;
                if (brace_depth == 0) {
                    obj_buf[obj_buf_len] = '\0';

                    ProcessRec r;
                    memset(&r, 0, sizeof(r));
                    json_get_int(obj_buf, "pid",  &r.pid);
                    json_get_str(obj_buf, "name", r.name, sizeof(r.name));
                    json_get_str(obj_buf, "workload_category",
                                 r.category, sizeof(r.category));
                    json_get_int(obj_buf, "nice_value", &r.nice);

                    if (r.pid <= 1 || r.name[0] == '\0') {
                        obj_buf_len    = 0;
                        top_brace_seen = 0;
                        continue;
                    }

                    apply_policy(&r);

                    rec_count++;
                    printf("  [%d] PID %-6d  %-20s  Category: %-22s  Nice: %3d\n",
                           rec_count, r.pid, r.name, r.category, r.nice);
                    printf("       Action: %-10s  Recommended Nice: %3d%s\n",
                           r.action, r.recommended_nice,
                           r.needs_root ? "  (needs sudo)" : "");
                    printf("       Rationale: %s\n\n", r.rationale);

                    if (strcmp(r.action, "boost")     == 0) boost_count++;
                    else if (strcmp(r.action, "throttle") == 0) throttle_count++;
                    else if (strcmp(r.action, "baseline") == 0) baseline_count++;
                    else nochange_count++;

                    log_action(audit_path, r.pid, r.name, r.nice,
                               r.recommended_nice, r.nice, 0,
                               "recommend", "recommendation", r.rationale);

                    obj_buf_len    = 0;
                    top_brace_seen = 0;
                }
            }
        }
    }

    fclose(f);

    if (rec_count == 0) {
        printf("  No process entries found in '%s'.\n", input_path);
        printf("  Ensure Module 1 has written a valid processes.json.\n\n");
        return 1;
    }

    printf("=== Recommendation Summary ===\n");
    printf("  Total processes evaluated : %d\n", rec_count);
    printf("  Boost    (lower nice)     : %d  (requires root/CAP_SYS_NICE)\n",
           boost_count);
    printf("  Throttle (raise nice)     : %d  (unprivileged)\n", throttle_count);
    printf("  Baseline (set to nice 0)  : %d\n", baseline_count);
    printf("  No change                 : %d\n", nochange_count);
    printf("\n  To apply any recommendation:\n");
    printf("    ./module2_governor/governor set-nice <PID> <NICE> [--yes]\n");
    printf("  For boosts (negative nice), use sudo:\n");
    printf("    sudo ./module2_governor/governor set-nice <PID> <NICE> --yes\n\n");

    return 0;
}

/* ============================================================
 * Usage
 * ============================================================ */
static void print_usage(const char *prog) {
    printf("\nDevGovernor Scheduling Priority Governor -- Module 2\n");
    printf("=====================================================\n\n");
    printf("Usage:\n");
    printf("  %s inspect      <PID>                       [--history <file>]\n",
           prog);
    printf("  %s evaluate     <PID> <TARGET_NICE>         [--history <file>]\n",
           prog);
    printf("  %s set-nice     <PID> <TARGET_NICE> [--yes] [--history <file>]\n",
           prog);
    printf("  %s recommend-all --input <processes.json>   [--history <file>]\n",
           prog);
    printf("\nOptions:\n");
    printf("  --history <file>   Audit log path (default: %s)\n",
           DEFAULT_AUDIT_FILE);
    printf("  --input   <file>   processes.json from Module 1\n");
    printf("  --yes | -y         Skip confirmation prompt for set-nice\n");
    printf("  --help | -h        Show this help\n");
    printf("\nNice Value Reference:\n");
    printf("  Range   : -20 (highest priority) to 19 (lowest priority)\n");
    printf("  Default :  0  (standard Linux CFS baseline)\n");
    printf("  Boost   : lower nice; requires CAP_SYS_NICE / sudo\n");
    printf("  Throttle: raise nice; unprivileged, any owning user\n");
    printf("\nSafety Guards:\n");
    printf("  PID 1 (init/systemd) and governor's own PID are protected.\n\n");
}

/* ============================================================
 * main
 * ============================================================ */
int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *cmd        = argv[1];
    const char *audit_path = DEFAULT_AUDIT_FILE;
    const char *input_path = NULL;
    int         auto_yes   = 0;

    /* Parse global flags */
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--history") == 0 && i + 1 < argc) {
            audit_path = argv[++i];
        } else if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
            input_path = argv[++i];
        } else if (strcmp(argv[i], "--yes") == 0 ||
                   strcmp(argv[i], "-y")    == 0) {
            auto_yes = 1;
        }
    }

    /* Route commands */
    if (strcmp(cmd, "inspect") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Error: 'inspect' requires <PID>\n");
            print_usage(argv[0]);
            return 1;
        }
        int pid = atoi(argv[2]);
        if (pid <= 0) {
            fprintf(stderr, "Error: Invalid PID '%s'\n", argv[2]);
            return 1;
        }
        return cmd_inspect(pid, audit_path);

    } else if (strcmp(cmd, "evaluate") == 0) {
        if (argc < 4) {
            fprintf(stderr,
                    "Error: 'evaluate' requires <PID> and <TARGET_NICE>\n");
            print_usage(argv[0]);
            return 1;
        }
        int pid         = atoi(argv[2]);
        int target_nice = atoi(argv[3]);
        return cmd_evaluate(pid, target_nice, audit_path);

    } else if (strcmp(cmd, "set-nice") == 0) {
        if (argc < 4) {
            fprintf(stderr,
                    "Error: 'set-nice' requires <PID> and <TARGET_NICE>\n");
            print_usage(argv[0]);
            return 1;
        }
        int pid         = atoi(argv[2]);
        int target_nice = atoi(argv[3]);
        return cmd_set_nice(pid, target_nice, auto_yes, audit_path);

    } else if (strcmp(cmd, "recommend-all") == 0) {
        if (!input_path) {
            fprintf(stderr,
                    "Error: 'recommend-all' requires --input <processes.json>\n");
            print_usage(argv[0]);
            return 1;
        }
        return cmd_recommend_all(input_path, audit_path);

    } else if (strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        print_usage(argv[0]);
        return 0;

    } else {
        fprintf(stderr, "Unknown command: '%s'\n", cmd);
        print_usage(argv[0]);
        return 1;
    }
}
