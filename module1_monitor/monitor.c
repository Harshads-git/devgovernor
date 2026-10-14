/**
 * ============================================================================
 * DevGovernor — Linux Process Monitor (Module 1)
 * ============================================================================
 * Student 1: Process Monitoring and Workload Intelligence
 *
 * Course: Operating Systems Lab (B.Tech IT)
 *
 * Purpose:
 *   Scans Linux /proc pseudo-filesystem to collect live process metrics:
 *     - PID & PPID
 *     - Process Name & State
 *     - Nice value (Scheduling priority)
 *     - Memory Resident Set Size (VmRSS in MB)
 *     - CPU utilization percentage (via two-point sampling of utime + stime)
 *     - Transparent workload category classification
 *   Exports valid JSON conforming to shared/schema.json.
 *
 * Key OS Concepts Demonstrated:
 *   1. /proc pseudo-filesystem: Virtual kernel interface presenting process state.
 *   2. Process Control Block (PCB) attributes: PID, PPID, execution states (R, S, D, Z).
 *   3. Scheduling Priorities: Linux nice values (-20 to 19).
 *   4. Memory Management: Virtual memory vs. Resident Set Size (RSS).
 *   5. CPU Time Accounting: User time (utime) and Kernel/System time (stime) in clock ticks.
 *   6. Robust Error Handling: Ephemeral process handling (processes terminating during scan).
 * ============================================================================
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <signal.h>

#define MAX_PROCESSES 4096
#define MAX_NAME_LEN 256
#define MAX_PATH_LEN 512
#define DEFAULT_INTERVAL_SEC 1
#define DEFAULT_SAMPLE_DELAY_MS 200

/* Global termination flag for graceful shutdown on SIGINT / SIGTERM */
static volatile int keep_running = 1;

void handle_signal(int sig) {
    (void)sig;
    keep_running = 0;
}

/* Data structure representing a single process snapshot */
typedef struct {
    int pid;
    int ppid;
    char name[MAX_NAME_LEN];
    char state[16];
    int nice_value;
    double memory_mb;
    int memory_available;     /* 1 if RSS was readable, 0 otherwise */
    unsigned long utime;      /* user mode CPU ticks */
    unsigned long stime;      /* kernel mode CPU ticks */
    double cpu_percent;
    int cpu_available;        /* 1 if CPU delta was calculated, 0 otherwise */
    char workload_category[32];
} ProcessInfo;

/* Heuristic Workload Classifier
 * Transparently tags processes based on executable name patterns.
 * Note for Viva: This is a heuristic classification, not a kernel-level guarantee.
 */
static void classify_workload(const char *name, char *category_out, size_t out_len) {
    if (!name || strlen(name) == 0) {
        snprintf(category_out, out_len, "system_or_unknown");
        return;
    }

    /* Convert to lowercase for case-insensitive matching */
    char lower[MAX_NAME_LEN];
    size_t i = 0;
    for (; name[i] && i < sizeof(lower) - 1; i++) {
        lower[i] = (char)tolower((unsigned char)name[i]);
    }
    lower[i] = '\0';

    /* 1. Build Processes */
    if (strstr(lower, "make") || strstr(lower, "ninja") ||
        strstr(lower, "gcc") || strstr(lower, "g++") ||
        strstr(lower, "clang") || strstr(lower, "cmake") ||
        strstr(lower, "gradle") || strstr(lower, "mvn") ||
        strstr(lower, "rustc") || strstr(lower, "ld") ||
        strstr(lower, "as") || strstr(lower, "cc1")) {
        snprintf(category_out, out_len, "build_process");
        return;
    }

    /* 2. Development Servers and Runtimes */
    if (strstr(lower, "node") || strstr(lower, "python") ||
        strstr(lower, "uvicorn") || strstr(lower, "gunicorn") ||
        strstr(lower, "flask") || strstr(lower, "django") ||
        strstr(lower, "ruby") || strstr(lower, "rails") ||
        strstr(lower, "java") || strstr(lower, "go") ||
        strstr(lower, "cargo") || strstr(lower, "vite") ||
        strstr(lower, "webpack")) {
        snprintf(category_out, out_len, "development_server");
        return;
    }

    /* 3. Development Tools and Interactive Editors/Shells */
    if (strstr(lower, "code") || strstr(lower, "cursor") ||
        strstr(lower, "nvim") || strstr(lower, "vim") ||
        strstr(lower, "emacs") || strstr(lower, "git") ||
        strstr(lower, "gdb") || strstr(lower, "lldb") ||
        strstr(lower, "valgrind") || strstr(lower, "strace") ||
        strstr(lower, "bash") || strstr(lower, "zsh") ||
        strstr(lower, "fish") || strstr(lower, "tmux") ||
        strstr(lower, "screen")) {
        snprintf(category_out, out_len, "development_tool");
        return;
    }

    /* 4. Background and Daemon Services */
    if (strstr(lower, "systemd") || strstr(lower, "cron") ||
        strstr(lower, "rsyslog") || strstr(lower, "dbus") ||
        strstr(lower, "snapd") || strstr(lower, "sshd") ||
        strstr(lower, "pulseaudio") || strstr(lower, "pipewire") ||
        strstr(lower, "sleep") || strstr(lower, "wsl")) {
        snprintf(category_out, out_len, "background_process");
        return;
    }

    /* 5. Default Fallback */
    snprintf(category_out, out_len, "system_or_unknown");
}

/* Helper to check if a directory entry name is purely numeric (a PID) */
static int is_pid_dir(const char *name) {
    if (!name || *name == '\0') return 0;
    for (const char *p = name; *p; p++) {
        if (!isdigit((unsigned char)*p)) return 0;
    }
    return 1;
}

/* Read process status from /proc/[pid]/status
 * Extracts: Name, State, PPid, and VmRSS (Resident Set Size).
 */
static int read_proc_status(int pid, char *name_out, size_t name_len,
                            char *state_out, size_t state_len,
                            int *ppid_out, double *mem_mb_out, int *mem_avail_out) {
    char path[MAX_PATH_LEN];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);

    FILE *f = fopen(path, "r");
    if (!f) {
        /* Process may have exited between readdir and fopen (ENOENT/ESRCH) */
        return -1;
    }

    char line[512];
    *ppid_out = 0;
    *mem_avail_out = 0;
    *mem_mb_out = 0.0;
    name_out[0] = '\0';
    state_out[0] = '\0';

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
            /* Take the single state letter, e.g., 'S', 'R', 'Z' */
            if (*p) {
                state_out[0] = *p;
                state_out[1] = '\0';
            }
        } else if (strncmp(line, "PPid:", 5) == 0) {
            *ppid_out = atoi(line + 5);
        } else if (strncmp(line, "VmRSS:", 6) == 0) {
            long rss_kb = 0;
            if (sscanf(line + 6, "%ld", &rss_kb) == 1) {
                *mem_mb_out = (double)rss_kb / 1024.0;
                *mem_avail_out = 1;
            }
        }
    }

    fclose(f);

    /* If process name is still empty, fallback to reading /proc/[pid]/comm */
    if (name_out[0] == '\0') {
        snprintf(path, sizeof(path), "/proc/%d/comm", pid);
        FILE *fc = fopen(path, "r");
        if (fc) {
            if (fgets(name_out, (int)name_len, fc)) {
                char *end = name_out + strlen(name_out) - 1;
                while (end >= name_out && (*end == '\n' || *end == '\r')) *end-- = '\0';
            }
            fclose(fc);
        }
    }

    if (state_out[0] == '\0') {
        snprintf(state_out, state_len, "?");
    }

    return 0;
}

/* Read CPU ticks (utime and stime) from /proc/[pid]/stat
 * Format: pid (comm) state ppid ... utime(14) stime(15) ...
 * Because 'comm' can contain spaces and parentheses, we locate the last ')'
 * and parse the fields after it.
 */
static int read_proc_stat_ticks(int pid, unsigned long *utime_out, unsigned long *stime_out) {
    char path[MAX_PATH_LEN];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);

    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char buffer[2048];
    if (!fgets(buffer, sizeof(buffer), f)) {
        fclose(f);
        return -1;
    }
    fclose(f);

    /* Find closing parenthesis */
    char *closing_paren = strrchr(buffer, ')');
    if (!closing_paren) return -1;

    /* Scan fields starting right after ") " */
    char state_char;
    int ppid, pgrp, session, tty_nr, tpgid;
    unsigned int flags;
    unsigned long minflt, cminflt, majflt, cmajflt;
    unsigned long utime = 0, stime = 0;

    int scanned = sscanf(closing_paren + 2,
                         "%c %d %d %d %d %d %u %lu %lu %lu %lu %lu %lu",
                         &state_char, &ppid, &pgrp, &session, &tty_nr, &tpgid,
                         &flags, &minflt, &cminflt, &majflt, &cmajflt,
                         &utime, &stime);

    if (scanned >= 13) {
        *utime_out = utime;
        *stime_out = stime;
        return 0;
    }

    return -1;
}

/* Escape string for safe JSON serialization */
static void escape_json_string(const char *src, char *dest, size_t dest_len) {
    size_t j = 0;
    for (size_t i = 0; src[i] && j + 2 < dest_len; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == '"') {
            dest[j++] = '\\';
            dest[j++] = '"';
        } else if (c == '\\') {
            dest[j++] = '\\';
            dest[j++] = '\\';
        } else if (c == '\b') {
            dest[j++] = '\\';
            dest[j++] = 'b';
        } else if (c == '\f') {
            dest[j++] = '\\';
            dest[j++] = 'f';
        } else if (c == '\n') {
            dest[j++] = '\\';
            dest[j++] = 'n';
        } else if (c == '\r') {
            dest[j++] = '\\';
            dest[j++] = 'r';
        } else if (c == '\t') {
            dest[j++] = '\\';
            dest[j++] = 't';
        } else if (c < 32) {
            /* Skip unprintable control characters */
            continue;
        } else {
            dest[j++] = (char)c;
        }
    }
    dest[j] = '\0';
}

/* Scan all processes currently running in /proc */
static int scan_all_processes(ProcessInfo *procs, int max_procs) {
    DIR *dir = opendir("/proc");
    if (!dir) {
        fprintf(stderr, "Error opening /proc: %s\n", strerror(errno));
        return -1;
    }

    struct dirent *entry;
    int count = 0;

    while ((entry = readdir(dir)) != NULL && count < max_procs) {
        if (!is_pid_dir(entry->d_name)) continue;

        int pid = atoi(entry->d_name);
        if (pid <= 0) continue;

        ProcessInfo *p = &procs[count];
        memset(p, 0, sizeof(ProcessInfo));
        p->pid = pid;

        /* 1. Read /proc/[pid]/status */
        if (read_proc_status(pid, p->name, sizeof(p->name),
                             p->state, sizeof(p->state),
                             &p->ppid, &p->memory_mb, &p->memory_available) != 0) {
            /* Process disappeared or inaccessible; safely skip */
            continue;
        }

        /* 2. Read Nice Value via getpriority() */
        errno = 0;
        int nice = getpriority(PRIO_PROCESS, pid);
        if (errno == 0) {
            p->nice_value = nice;
        } else {
            /* Fallback nice value to 0 if getpriority fails due to permission */
            p->nice_value = 0;
        }

        /* 3. Read CPU ticks for sampling */
        if (read_proc_stat_ticks(pid, &p->utime, &p->stime) == 0) {
            p->cpu_available = 1;
        } else {
            p->cpu_available = 0;
            p->utime = 0;
            p->stime = 0;
        }

        /* 4. Heuristic classification */
        classify_workload(p->name, p->workload_category, sizeof(p->workload_category));

        count++;
    }

    closedir(dir);
    return count;
}

/* Calculate CPU utilization across two sampling points
 * Formula:
 *   delta_ticks = (utime2 + stime2) - (utime1 + stime1)
 *   elapsed_seconds = wall-clock time difference
 *   cpu_percent = (delta_ticks / sysconf(_SC_CLK_TCK)) / elapsed_seconds * 100.0
 */
static void calculate_cpu_usage(ProcessInfo *sample1, int count1,
                                ProcessInfo *sample2, int count2,
                                double elapsed_seconds) {
    long clk_tck = sysconf(_SC_CLK_TCK);
    if (clk_tck <= 0) clk_tck = 100; /* Standard Linux default is 100Hz */

    if (elapsed_seconds <= 0.0) elapsed_seconds = 0.001;

    for (int i = 0; i < count2; i++) {
        ProcessInfo *p2 = &sample2[i];
        p2->cpu_percent = 0.0;
        p2->cpu_available = 0;

        /* Find matching PID in sample1 */
        for (int j = 0; j < count1; j++) {
            ProcessInfo *p1 = &sample1[j];
            if (p1->pid == p2->pid) {
                unsigned long total_ticks1 = p1->utime + p1->stime;
                unsigned long total_ticks2 = p2->utime + p2->stime;

                if (total_ticks2 >= total_ticks1) {
                    unsigned long delta_ticks = total_ticks2 - total_ticks1;
                    double usage = ((double)delta_ticks / (double)clk_tck) / elapsed_seconds * 100.0;
                    if (usage < 0.0) usage = 0.0;
                    p2->cpu_percent = usage;
                    p2->cpu_available = 1;
                }
                break;
            }
        }
    }
}

/* Atomically write JSON snapshot conforming to shared/schema.json */
static int export_json_snapshot(const char *output_path, ProcessInfo *procs, int count,
                                double sample_interval) {
    char tmp_path[MAX_PATH_LEN + 16];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", output_path);

    FILE *f = fopen(tmp_path, "w");
    if (!f) {
        fprintf(stderr, "Error creating output file %s: %s\n", tmp_path, strerror(errno));
        return -1;
    }

    /* Generate ISO-8601 UTC timestamp */
    time_t now = time(NULL);
    struct tm tm_utc;
    gmtime_r(&now, &tm_utc);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);

    /* Write JSON header */
    fprintf(f, "{\n");
    fprintf(f, "  \"schema_version\": 1,\n");
    fprintf(f, "  \"timestamp\": \"%s\",\n", time_str);
    fprintf(f, "  \"sampling_interval_seconds\": %.2f,\n", sample_interval);
    fprintf(f, "  \"process_count\": %d,\n", count);
    fprintf(f, "  \"processes\": [\n");

    char escaped_name[MAX_NAME_LEN * 2];

    for (int i = 0; i < count; i++) {
        ProcessInfo *p = &procs[i];
        escape_json_string(p->name, escaped_name, sizeof(escaped_name));

        fprintf(f, "    {\n");
        fprintf(f, "      \"pid\": %d,\n", p->pid);
        fprintf(f, "      \"ppid\": %d,\n", p->ppid);
        fprintf(f, "      \"name\": \"%s\",\n", escaped_name);
        fprintf(f, "      \"state\": \"%s\",\n", p->state);

        if (p->cpu_available) {
            fprintf(f, "      \"cpu_percent\": %.1f,\n", p->cpu_percent);
        } else {
            fprintf(f, "      \"cpu_percent\": null,\n");
        }

        if (p->memory_available) {
            fprintf(f, "      \"memory_mb\": %.1f,\n", p->memory_mb);
        } else {
            fprintf(f, "      \"memory_mb\": null,\n");
        }

        fprintf(f, "      \"nice_value\": %d,\n", p->nice_value);
        fprintf(f, "      \"workload_category\": \"%s\"\n", p->workload_category);

        if (i < count - 1) {
            fprintf(f, "    },\n");
        } else {
            fprintf(f, "    }\n");
        }
    }

    fprintf(f, "  ]\n");
    fprintf(f, "}\n");

    fclose(f);

    /* Atomic rename to prevent readers from seeing partial writes */
    if (rename(tmp_path, output_path) != 0) {
        fprintf(stderr, "Error renaming %s to %s: %s\n", tmp_path, output_path, strerror(errno));
        return -1;
    }

    return 0;
}

static void print_usage(const char *prog) {
    printf("DevGovernor Process Monitor (Module 1)\n");
    printf("Usage:\n");
    printf("  %s [options]\n\n", prog);
    printf("Options:\n");
    printf("  --output <file>       Path to output JSON file (default: data/processes.json)\n");
    printf("  --interval <seconds>  Sampling interval in seconds (default: 1)\n");
    printf("  --oneshot             Take a single two-point sample, write JSON, and exit\n");
    printf("  --iterations <N>      Run exactly N sampling cycles and exit\n");
    printf("  --help                Show this help message\n");
}

int main(int argc, char *argv[]) {
    const char *output_file = "data/processes.json";
    int interval_sec = DEFAULT_INTERVAL_SEC;
    int oneshot = 0;
    int max_iterations = -1;

    /* Parse command-line arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            output_file = argv[++i];
        } else if (strcmp(argv[i], "--interval") == 0 && i + 1 < argc) {
            interval_sec = atoi(argv[++i]);
            if (interval_sec <= 0) interval_sec = 1;
        } else if (strcmp(argv[i], "--oneshot") == 0) {
            oneshot = 1;
        } else if (strcmp(argv[i], "--iterations") == 0 && i + 1 < argc) {
            max_iterations = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    /* Register signal handlers for clean interruption */
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    printf("[DevGovernor Monitor] Starting process monitor...\n");
    printf("  Output target: %s\n", output_file);
    printf("  Interval:      %d second(s)\n", interval_sec);
    if (oneshot) {
        printf("  Mode:          One-shot snapshot\n");
    } else if (max_iterations > 0) {
        printf("  Mode:          Fixed %d iteration(s)\n", max_iterations);
    } else {
        printf("  Mode:          Continuous monitoring (Ctrl+C to stop)\n");
    }

    /* Allocate process buffers on heap */
    ProcessInfo *sample1 = calloc(MAX_PROCESSES, sizeof(ProcessInfo));
    ProcessInfo *sample2 = calloc(MAX_PROCESSES, sizeof(ProcessInfo));
    if (!sample1 || !sample2) {
        fprintf(stderr, "Error allocating process memory buffers\n");
        free(sample1);
        free(sample2);
        return 1;
    }

    int iteration = 0;

    while (keep_running) {
        struct timespec t_start, t_end;
        clock_gettime(CLOCK_MONOTONIC, &t_start);

        /* First point sample */
        int count1 = scan_all_processes(sample1, MAX_PROCESSES);
        if (count1 < 0) {
            fprintf(stderr, "Failed to scan /proc\n");
            break;
        }

        /* Sleep duration between two points for CPU delta */
        if (oneshot) {
            usleep(DEFAULT_SAMPLE_DELAY_MS * 1000);
        } else {
            sleep(interval_sec);
        }

        clock_gettime(CLOCK_MONOTONIC, &t_end);
        double elapsed_sec = (t_end.tv_sec - t_start.tv_sec) +
                             (t_end.tv_nsec - t_start.tv_nsec) / 1e9;

        /* Second point sample */
        int count2 = scan_all_processes(sample2, MAX_PROCESSES);
        if (count2 < 0) {
            fprintf(stderr, "Failed to scan /proc second time\n");
            break;
        }

        /* Compute CPU utilization across the two samples */
        calculate_cpu_usage(sample1, count1, sample2, count2, elapsed_sec);

        /* Export JSON */
        if (export_json_snapshot(output_file, sample2, count2, elapsed_sec) == 0) {
            printf("[DevGovernor Monitor] Snapshot written (%d processes, %.2fs elapsed)\n",
                   count2, elapsed_sec);
        } else {
            fprintf(stderr, "[DevGovernor Monitor] Failed to write snapshot\n");
        }

        iteration++;
        if (oneshot || (max_iterations > 0 && iteration >= max_iterations)) {
            break;
        }
    }

    printf("[DevGovernor Monitor] Stopped cleanly.\n");
    free(sample1);
    free(sample2);
    return 0;
}
