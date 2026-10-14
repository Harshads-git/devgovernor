/**
 * ============================================================================
 * DevGovernor — Scheduling Priority Governor (Module 2)
 * ============================================================================
 * Student 2: Resource Management and Scheduling Governor
 *
 * Course: Operating Systems Lab (B.Tech IT)
 *
 * Purpose:
 *   Manages Linux process scheduling priorities via the standard POSIX / Linux
 *   APIs (getpriority, setpriority).
 *
 * Core Capabilities:
 *   1. inspect   <PID>: Inspect process state, memory, and current nice priority.
 *   2. evaluate  <PID> <TARGET_NICE>: Dry-run evaluation and policy recommendation.
 *   3. set-nice  <PID> <TARGET_NICE>: Explicit, validated nice value modification.
 *   4. JSON-Lines Logging: Appends audit trail to data/actions.jsonl.
 *
 * Safety & OS Principles:
 *   - Never modifies PID 1 (init/systemd) or self PID.
 *   - Strictly validates nice value range [-20, 19].
 *   - Differentiates between raising nice (lower CPU priority, unprivileged)
 *     and lowering nice (higher CPU priority, requires CAP_SYS_NICE or root).
 *   - Safe error handling: handles ESRCH (process vanished) and EACCES/EPERM.
 *   - No automatic/uncontrolled background modifications.
 * ============================================================================
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>

#define DEFAULT_HISTORY_FILE "data/actions.jsonl"
#define MAX_NAME_LEN 256
#define MAX_MSG_LEN 512

/* Read process name and basic info from /proc/[pid]/status */
static int get_process_info(int pid, char *name_out, size_t name_len,
                            char *state_out, size_t state_len,
                            int *ppid_out, double *mem_mb_out) {
    (void)state_len;
    char path[256];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);

    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char line[256];
    name_out[0] = '\0';
    state_out[0] = '\0';
    *ppid_out = 0;
    *mem_mb_out = 0.0;

    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "Name:", 5) == 0) {
            char *p = line + 5;
            while (*p == ' ' || *p == '\t') p++;
            char *end = p + strlen(p) - 1;
            while (end > p && (*end == '\n' || *end == '\r' || *end == ' ')) *end-- = '\0';
            snprintf(name_out, name_len, "%s", p);
        } else if (strncmp(line, "State:", 6) == 0) {
            char *p = line + 6;
            while (*p == ' ' || *p == '\t') p++;
            if (*p) {
                state_out[0] = *p;
                state_out[1] = '\0';
            }
        } else if (strncmp(line, "PPid:", 5) == 0) {
            *ppid_out = atoi(line + 5);
        } else if (strncmp(line, "VmRSS:", 6) == 0) {
            long kb = 0;
            if (sscanf(line + 6, "%ld", &kb) == 1) {
                *mem_mb_out = (double)kb / 1024.0;
            }
        }
    }

    fclose(f);
    return 0;
}

/* Get current nice value using getpriority()
 * Note for Viva: getpriority returns -1 both on error and as a valid nice value!
 * Therefore, errno must be cleared to 0 prior to calling getpriority.
 */
static int get_process_nice(int pid, int *nice_out) {
    errno = 0;
    int val = getpriority(PRIO_PROCESS, pid);
    if (errno != 0) {
        return -1;
    }
    *nice_out = val;
    return 0;
}

/* Append structured record to JSON-lines history file */
static void log_action(const char *history_path, int pid, const char *process_name,
                       int prev_nice, int req_nice, int resulting_nice,
                       int has_result, const char *action_type,
                       const char *status, const char *message) {
    FILE *f = fopen(history_path, "a");
    if (!f) {
        fprintf(stderr, "[Governor Warning] Could not open history file %s: %s\n",
                history_path, strerror(errno));
        return;
    }

    /* Timestamp in ISO-8601 UTC */
    time_t now = time(NULL);
    struct tm tm_utc;
    gmtime_r(&now, &tm_utc);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);

    fprintf(f, "{\"timestamp\": \"%s\", \"pid\": %d, \"process_name\": \"%s\", ",
            time_str, pid, process_name ? process_name : "unknown");

    if (prev_nice >= -20 && prev_nice <= 19) {
        fprintf(f, "\"previous_nice\": %d, ", prev_nice);
    } else {
        fprintf(f, "\"previous_nice\": null, ");
    }

    if (req_nice >= -20 && req_nice <= 19) {
        fprintf(f, "\"requested_nice\": %d, ", req_nice);
    } else {
        fprintf(f, "\"requested_nice\": null, ");
    }

    if (has_result && resulting_nice >= -20 && resulting_nice <= 19) {
        fprintf(f, "\"resulting_nice\": %d, ", resulting_nice);
    } else {
        fprintf(f, "\"resulting_nice\": null, ");
    }

    fprintf(f, "\"action_type\": \"%s\", \"status\": \"%s\", \"message\": \"%s\"}\n",
            action_type, status, message);

    fclose(f);
}

/* Command: inspect <PID> */
static int cmd_inspect(int pid, const char *history_path) {
    char name[MAX_NAME_LEN] = "unknown";
    char state[16] = "?";
    int ppid = 0;
    double mem_mb = 0.0;
    int current_nice = 0;

    if (get_process_info(pid, name, sizeof(name), state, sizeof(state), &ppid, &mem_mb) != 0) {
        fprintf(stderr, "Error: PID %d not found or inaccessible (check if process exists in /proc).\n", pid);
        return 1;
    }

    if (get_process_nice(pid, &current_nice) != 0) {
        fprintf(stderr, "Error reading nice value for PID %d: %s\n", pid, strerror(errno));
        return 1;
    }

    printf("\n=== DevGovernor Process Inspection ===\n");
    printf("  PID:            %d\n", pid);
    printf("  Name:           %s\n", name);
    printf("  PPID:           %d\n", ppid);
    printf("  State:          %s\n", state);
    printf("  Resident Mem:   %.2f MB\n", mem_mb);
    printf("  Current Nice:   %d\n", current_nice);

    printf("\nScheduling Context (CFS):\n");
    if (current_nice < 0) {
        printf("  Priority Class: HIGH PRIORITY (Lower nice = higher CPU weight)\n");
    } else if (current_nice == 0) {
        printf("  Priority Class: STANDARD PRIORITY (Default Linux baseline)\n");
    } else {
        printf("  Priority Class: LOW / BACKGROUND PRIORITY (Higher nice = lower CPU weight)\n");
    }
    printf("  Valid Nice Range: [-20 (highest priority) to 19 (lowest priority)]\n\n");

    /* Log inspection */
    log_action(history_path, pid, name, current_nice, -999, current_nice, 1,
               "inspect", "success", "Inspected process scheduling priority and state");

    return 0;
}

/* Command: evaluate <PID> <TARGET_NICE> (Dry-run / Policy Recommendation) */
static int cmd_evaluate(int pid, int target_nice, const char *history_path) {
    /* Safety Guard 1: Root init process check */
    if (pid <= 1) {
        fprintf(stderr, "Evaluation Rejected: Target PID %d is invalid or reserved (init/systemd).\n", pid);
        log_action(history_path, pid, "init/root", -999, target_nice, -999, 0,
                   "evaluate", "rejected", "Target PID is reserved (PID <= 1)");
        return 1;
    }

    /* Safety Guard 2: Self-PID check */
    if (pid == getpid()) {
        fprintf(stderr, "Evaluation Rejected: Target PID %d is the governor's own PID.\n", pid);
        log_action(history_path, pid, "devgovernor", -999, target_nice, -999, 0,
                   "evaluate", "rejected", "Target PID is the governor itself");
        return 1;
    }

    /* Safety Guard 3: Nice range validation */
    if (target_nice < -20 || target_nice > 19) {
        fprintf(stderr, "Evaluation Rejected: Target nice value %d is out of range [-20, 19].\n", target_nice);
        log_action(history_path, pid, "unknown", -999, target_nice, -999, 0,
                   "evaluate", "rejected", "Nice value out of range [-20, 19]");
        return 1;
    }

    char name[MAX_NAME_LEN] = "unknown";
    char state[16] = "?";
    int ppid = 0;
    double mem_mb = 0.0;
    int current_nice = 0;

    if (get_process_info(pid, name, sizeof(name), state, sizeof(state), &ppid, &mem_mb) != 0) {
        fprintf(stderr, "Evaluation Failed: Process with PID %d does not exist.\n", pid);
        log_action(history_path, pid, "unknown", -999, target_nice, -999, 0,
                   "evaluate", "failed", "Process not found in /proc");
        return 1;
    }

    if (get_process_nice(pid, &current_nice) != 0) {
        fprintf(stderr, "Error inspecting PID %d: %s\n", pid, strerror(errno));
        return 1;
    }

    printf("\n=== DevGovernor Priority Evaluation (Dry-Run) ===\n");
    printf("  PID:            %d (%s)\n", pid, name);
    printf("  Current Nice:   %d\n", current_nice);
    printf("  Proposed Nice:  %d\n", target_nice);

    char msg[MAX_MSG_LEN];

    if (target_nice == current_nice) {
        printf("  Recommendation: NO CHANGE (Target nice matches current nice)\n");
        snprintf(msg, sizeof(msg), "Proposed nice %d is identical to current nice %d", target_nice, current_nice);
    } else if (target_nice < current_nice) {
        printf("  Recommendation: BOOST PRIORITY (Lower nice value gives more CPU time)\n");
        printf("  Privilege Note: Lowering nice requires superuser privileges (CAP_SYS_NICE or sudo).\n");
        snprintf(msg, sizeof(msg), "Recommend boost from %d to %d (requires root/CAP_SYS_NICE)",
                 current_nice, target_nice);
    } else {
        printf("  Recommendation: THROTTLE / BACKGROUND (Higher nice value frees CPU for foreground dev tasks)\n");
        printf("  Privilege Note: Raising nice value usually succeeds for processes owned by current user.\n");
        snprintf(msg, sizeof(msg), "Recommend throttle from %d to %d (standard user privilege)",
                 current_nice, target_nice);
    }

    printf("  Dry-Run Result: No changes were applied to PID %d.\n\n", pid);

    log_action(history_path, pid, name, current_nice, target_nice, current_nice, 1,
               "evaluate", "recommendation", msg);

    return 0;
}

/* Command: set-nice <PID> <TARGET_NICE> (Explicit, validated modification) */
static int cmd_set_nice(int pid, int target_nice, const char *history_path) {
    /* Safety Guard 1: Root init process check */
    if (pid <= 1) {
        fprintf(stderr, "Modification Blocked: Target PID %d is init/systemd (protected).\n", pid);
        log_action(history_path, pid, "init/root", -999, target_nice, -999, 0,
                   "set_nice", "rejected", "Modification of PID <= 1 blocked");
        return 1;
    }

    /* Safety Guard 2: Self-PID check */
    if (pid == getpid()) {
        fprintf(stderr, "Modification Blocked: Cannot alter governor's own priority.\n");
        log_action(history_path, pid, "devgovernor", -999, target_nice, -999, 0,
                   "set_nice", "rejected", "Modification of governor self PID blocked");
        return 1;
    }

    /* Safety Guard 3: Range check */
    if (target_nice < -20 || target_nice > 19) {
        fprintf(stderr, "Error: Target nice value %d is out of range [-20, 19].\n", target_nice);
        log_action(history_path, pid, "unknown", -999, target_nice, -999, 0,
                   "set_nice", "rejected", "Target nice value outside [-20, 19]");
        return 1;
    }

    char name[MAX_NAME_LEN] = "unknown";
    char state[16] = "?";
    int ppid = 0;
    double mem_mb = 0.0;
    int current_nice = 0;

    if (get_process_info(pid, name, sizeof(name), state, sizeof(state), &ppid, &mem_mb) != 0) {
        fprintf(stderr, "Error: Target process PID %d does not exist or exited.\n", pid);
        log_action(history_path, pid, "unknown", -999, target_nice, -999, 0,
                   "set_nice", "failed", "Process not found in /proc (ESRCH)");
        return 1;
    }

    if (get_process_nice(pid, &current_nice) != 0) {
        fprintf(stderr, "Error: Could not retrieve current nice value for PID %d.\n", pid);
        return 1;
    }

    printf("\n[DevGovernor Warning] You are about to alter process priority:\n");
    printf("  PID:            %d (%s)\n", pid, name);
    printf("  Current Nice:   %d\n", current_nice);
    printf("  Requested Nice: %d\n", target_nice);

    /* Execute system call setpriority(PRIO_PROCESS, pid, target_nice) */
    errno = 0;
    int ret = setpriority(PRIO_PROCESS, pid, target_nice);

    if (ret != 0) {
        char err_msg[MAX_MSG_LEN];
        snprintf(err_msg, sizeof(err_msg), "setpriority failed: %s", strerror(errno));
        fprintf(stderr, "Error: %s\n", err_msg);

        if (errno == EPERM || errno == EACCES) {
            fprintf(stderr, "Hint: Lowering nice values (< current) or modifying processes owned by other\n");
            fprintf(stderr, "      users requires superuser privileges (sudo or CAP_SYS_NICE).\n");
        }

        log_action(history_path, pid, name, current_nice, target_nice, current_nice, 1,
                   "set_nice", "failed", err_msg);
        return 1;
    }

    /* Verify resulting nice value */
    int verified_nice = 0;
    if (get_process_nice(pid, &verified_nice) == 0) {
        printf("Success: Process PID %d (%s) nice priority updated from %d to %d.\n\n",
               pid, name, current_nice, verified_nice);

        char success_msg[MAX_MSG_LEN];
        snprintf(success_msg, sizeof(success_msg), "Successfully changed nice from %d to %d",
                 current_nice, verified_nice);

        log_action(history_path, pid, name, current_nice, target_nice, verified_nice, 1,
                   "set_nice", "success", success_msg);
    } else {
        printf("Notice: setpriority succeeded, but could not re-verify resulting nice value.\n");
        log_action(history_path, pid, name, current_nice, target_nice, target_nice, 1,
                   "set_nice", "success", "Updated nice value (unverified post-check)");
    }

    return 0;
}

static void print_usage(const char *prog) {
    printf("DevGovernor Scheduling Priority Governor (Module 2)\n");
    printf("Usage:\n");
    printf("  %s inspect  <PID>              [--history <file>]\n", prog);
    printf("  %s evaluate <PID> <TARGET_NICE> [--history <file>]\n", prog);
    printf("  %s set-nice <PID> <TARGET_NICE> [--history <file>]\n", prog);
    printf("\nOptions:\n");
    printf("  --history <file>   JSON-Lines audit log path (default: data/actions.jsonl)\n");
    printf("  --help             Show this help screen\n");
    printf("\nNote on Nice Values:\n");
    printf("  Valid range is -20 (highest priority) to 19 (lowest priority).\n");
    printf("  Raising nice value (e.g. 0 -> 10) deprioritizes background tasks.\n");
    printf("  Lowering nice value (e.g. 0 -> -5) prioritizes build/dev tools (requires sudo/CAP_SYS_NICE).\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *cmd = argv[1];
    const char *history_path = DEFAULT_HISTORY_FILE;

    /* Parse --history override if present in args */
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--history") == 0 && i + 1 < argc) {
            history_path = argv[i + 1];
        }
    }

    if (strcmp(cmd, "inspect") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Error: inspect requires <PID>\n");
            print_usage(argv[0]);
            return 1;
        }
        int pid = atoi(argv[2]);
        if (pid <= 0) {
            fprintf(stderr, "Error: Invalid PID '%s'\n", argv[2]);
            return 1;
        }
        return cmd_inspect(pid, history_path);
    } else if (strcmp(cmd, "evaluate") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Error: evaluate requires <PID> and <TARGET_NICE>\n");
            print_usage(argv[0]);
            return 1;
        }
        int pid = atoi(argv[2]);
        int target_nice = atoi(argv[3]);
        return cmd_evaluate(pid, target_nice, history_path);
    } else if (strcmp(cmd, "set-nice") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Error: set-nice requires <PID> and <TARGET_NICE>\n");
            print_usage(argv[0]);
            return 1;
        }
        int pid = atoi(argv[2]);
        int target_nice = atoi(argv[3]);
        return cmd_set_nice(pid, target_nice, history_path);
    } else if (strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        print_usage(argv[0]);
        return 0;
    } else {
        fprintf(stderr, "Unknown command: %s\n", cmd);
        print_usage(argv[0]);
        return 1;
    }
}
