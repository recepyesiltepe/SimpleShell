#define _POSIX_C_SOURCE 200809L

#include "jobs.h"

#include <stdbool.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include "memory.h"

typedef struct {
    int id;
    pid_t pid;
    char *command;
    bool stopped;
    bool done;
    int wait_status;
} Job;

static Job *jobs = NULL;
static int jobs_count = 0;
static int jobs_capacity = 0;
static int next_job_id = 1;

static int exit_status_from_wait(int wait_status) {
    if (WIFEXITED(wait_status)) {
        return WEXITSTATUS(wait_status);
    }
    if (WIFSIGNALED(wait_status)) {
        return 128 + WTERMSIG(wait_status);
    }
    return 1;
}

static void ensure_capacity(void) {
    if (jobs_count < jobs_capacity) {
        return;
    }
    jobs_capacity = jobs_capacity == 0 ? 8 : jobs_capacity * 2;
    jobs = xrealloc(jobs, (size_t)jobs_capacity * sizeof(Job));
}

static int find_job_index_by_pid(pid_t pid) {
    for (int i = 0; i < jobs_count; i++) {
        if (jobs[i].pid == pid) {
            return i;
        }
    }
    return -1;
}

static int find_job_index_by_id(int id) {
    for (int i = 0; i < jobs_count; i++) {
        if (jobs[i].id == id) {
            return i;
        }
    }
    return -1;
}

static int find_latest_job_index(void) {
    if (jobs_count == 0) {
        return -1;
    }
    int index = 0;
    for (int i = 1; i < jobs_count; i++) {
        if (jobs[i].id > jobs[index].id) {
            index = i;
        }
    }
    return index;
}

static void remove_job_index(int index) {
    free(jobs[index].command);
    for (int i = index; i < jobs_count - 1; i++) {
        jobs[i] = jobs[i + 1];
    }
    jobs_count--;
}

void jobs_reap_finished(void) {
    int wait_status = 0;
    pid_t pid = 0;
    while ((pid = waitpid(-1, &wait_status, WNOHANG | WUNTRACED)) > 0) {
        int index = find_job_index_by_pid(pid);
        if (index >= 0) {
            jobs[index].wait_status = wait_status;
            if (WIFEXITED(wait_status) || WIFSIGNALED(wait_status)) {
                jobs[index].done = true;
                jobs[index].stopped = false;
            } else if (WIFSTOPPED(wait_status)) {
                jobs[index].stopped = true;
            }
        }
    }
}

static int continue_job(Job *job) {
    if (kill(job->pid, SIGCONT) != 0) {
        perror("kill(SIGCONT)");
        return 1;
    }
    job->stopped = false;
    return 0;
}

static int get_job_index_for_request(int requested_id, const char *builtin_name) {
    int index = requested_id < 0 ? find_latest_job_index() : find_job_index_by_id(requested_id);
    if (index < 0) {
        fprintf(stderr, "%s: no such job\n", builtin_name);
        return -1;
    }
    return index;
}

static void print_job_line(const Job *job) {
    const char *state = "Running";
    if (job->done) {
        state = "Done";
    } else if (job->stopped) {
        state = "Stopped";
    }
    printf("[%d] %s %s\n", job->id, state, job->command);
}

int jobs_background(int requested_id) {
    jobs_reap_finished();

    int index = get_job_index_for_request(requested_id, "bg");
    if (index < 0) {
        return 1;
    }

    Job *job = &jobs[index];
    if (job->done) {
        fprintf(stderr, "bg: job already finished\n");
        remove_job_index(index);
        return 1;
    }

    if (job->stopped && continue_job(job) != 0) {
        return 1;
    }

    print_job_line(job);
    return 0;
}

static int wait_foreground_job(Job *job, int *wait_status) {
    while (1) {
        pid_t wait_result = waitpid(job->pid, wait_status, WUNTRACED);
        if (wait_result < 0) {
            perror("waitpid");
            return 1;
        }
        if (WIFSTOPPED(*wait_status) || WIFEXITED(*wait_status) || WIFSIGNALED(*wait_status)) {
            return 0;
        }
    }
}

int jobs_foreground(int requested_id, int *exit_status) {
    jobs_reap_finished();

    int index = get_job_index_for_request(requested_id, "fg");
    if (index < 0) {
        return 1;
    }

    Job *job = &jobs[index];
    printf("%s\n", job->command);

    if (job->done) {
        *exit_status = exit_status_from_wait(job->wait_status);
        remove_job_index(index);
        return 0;
    }

    if (job->stopped && continue_job(job) != 0) {
        return 1;
    }

    int wait_status = 0;
    if (wait_foreground_job(job, &wait_status) != 0) {
        return 1;
    }

    if (WIFSTOPPED(wait_status)) {
        job->stopped = true;
        job->wait_status = wait_status;
        *exit_status = 128 + WSTOPSIG(wait_status);
        return 0;
    }

    *exit_status = exit_status_from_wait(wait_status);
    remove_job_index(index);
    return 0;
}

int jobs_add(pid_t pid, const char *command) {
    ensure_capacity();
    Job *job = &jobs[jobs_count++];
    job->id = next_job_id++;
    job->pid = pid;
    job->command = xstrdup(command);
    job->stopped = false;
    job->done = false;
    job->wait_status = 0;
    return job->id;
}

int jobs_print(void) {
    jobs_reap_finished();
    if (jobs_count == 0) {
        return 0;
    }

    for (int i = 0; i < jobs_count; i++) {
        print_job_line(&jobs[i]);
    }

    for (int i = jobs_count - 1; i >= 0; i--) {
        if (jobs[i].done) {
            remove_job_index(i);
        }
    }

    return 0;
}

void jobs_cleanup(void) {
    for (int i = 0; i < jobs_count; i++) {
        free(jobs[i].command);
    }
    free(jobs);
    jobs = NULL;
    jobs_count = 0;
    jobs_capacity = 0;
}
