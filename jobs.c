#include "jobs.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include "memory.h"

typedef struct {
    int id;
    pid_t pid;
    char *command;
    bool finished;
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
    while ((pid = waitpid(-1, &wait_status, WNOHANG)) > 0) {
        int index = find_job_index_by_pid(pid);
        if (index >= 0) {
            jobs[index].finished = true;
            jobs[index].wait_status = wait_status;
        }
    }
}

int jobs_add(pid_t pid, const char *command) {
    ensure_capacity();
    Job *job = &jobs[jobs_count++];
    job->id = next_job_id++;
    job->pid = pid;
    job->command = xstrdup(command);
    job->finished = false;
    job->wait_status = 0;
    return job->id;
}

int jobs_print(void) {
    jobs_reap_finished();
    if (jobs_count == 0) {
        return 0;
    }

    for (int i = 0; i < jobs_count; i++) {
        const char *state = jobs[i].finished ? "Done" : "Running";
        printf("[%d] %s %s\n", jobs[i].id, state, jobs[i].command);
    }

    for (int i = jobs_count - 1; i >= 0; i--) {
        if (jobs[i].finished) {
            remove_job_index(i);
        }
    }

    return 0;
}

int jobs_foreground(int requested_id, int *exit_status) {
    jobs_reap_finished();

    int index = requested_id < 0 ? find_latest_job_index() : find_job_index_by_id(requested_id);
    if (index < 0) {
        fprintf(stderr, "fg: no such job\n");
        return 1;
    }

    Job *job = &jobs[index];
    printf("%s\n", job->command);

    int wait_status = job->wait_status;
    if (!job->finished) {
        if (waitpid(job->pid, &wait_status, 0) < 0) {
            perror("waitpid");
            return 1;
        }
    }

    *exit_status = exit_status_from_wait(wait_status);
    remove_job_index(index);
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
