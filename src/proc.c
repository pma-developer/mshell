#include "proc.h"
#include "util.h"
#include "vec.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>

void freeProcessFields(const Process *process) {
    LOG("freeing process->name %p\n", process->name);
    free((void *)process->name);
}

void printProcess(const Process *proc) {
    LOG("Process PID: %d\n", proc->pid);
    LOG("Process Name: %s\n", proc->name);
    LOG("Process Status: %d\n", (int)proc->status);
}
void printJob(const Job *job) {
    if (job == NULL) {
        LOG("Job is NULL.\n");
        return;
    }
    LOG("Job PGID: %d\n", job->pgid);
    LOG("========================\n");

    size_t index = 0;
    while (index < job->pipeline.count) {
        printProcess(&job->pipeline.items[index]);
        LOG("------------------------\n");
        index++;
    }
}

void printWStatusDesc(int wstatus, const char *procname, pid_t pid) {
    if (WIFEXITED(wstatus)) {
        int exit_status = WEXITSTATUS(wstatus);
        if (exit_status != 0) {
            printf("\n%s(%i) exited with status %i\n", procname, pid, exit_status);
        }
    }
    if (WIFSIGNALED(wstatus)) {
        int sig_num = WTERMSIG(wstatus);
        printf("\n%s(%i) terminated by signal %i\n", procname, pid, sig_num);

        bool produced_core_dump = false;
#ifdef WCOREDUMP
        produced_core_dump = WCOREDUMP(wstatus);
        if (produced_core_dump) {
            printf("Produced core dump.\n");
        }
#endif
    }
    if (WIFSTOPPED(wstatus)) {
        int sig_num = WSTOPSIG(wstatus);
        printf("\n%s(%i) stopped by signal %i\n", procname, pid, sig_num);
    }
    if (WIFCONTINUED(wstatus)) {
        printf("\n%s(%i) continued\n", procname, pid);
    }
}

ProcStatus waitProcStatus(pid_t pid) {
    int wstatus = 0;
    pid_t wait_res = waitpid(pid, &wstatus, WNOHANG | WUNTRACED | WCONTINUED);
    if (wait_res == -1) {
        LOG("errno=%i\n", errno);
        perror("waitpid(WNOHANG | WUNTRACED)");
        return PROC_STATUS_UNDEFINED;
    }
    if (wait_res == 0) {
        return PROC_STATUS_NOT_CHANGED;
    }
    if (WIFEXITED(wstatus) || WIFSIGNALED(wstatus)) {
        printWStatusDesc(wstatus, "proc", pid);
        return PROC_STATUS_TERMINATED;
    }

    if (WIFSTOPPED(wstatus)) {
        printWStatusDesc(wstatus, "proc", pid);
        return PROC_STATUS_SUSPENDED;
    }

    if (WIFCONTINUED(wstatus)) {
        printWStatusDesc(wstatus, "proc", pid);
        return PROC_STATUS_RUNNING;
    }
    return PROC_STATUS_UNDEFINED;
}


bool anyProcessStatus(const ProcessVec *process_vec, ProcStatus status) {
    for (size_t i = 0; i < process_vec->count; ++i) {
        if (process_vec->items[i].status == status) {
            return true;
        }
    }
    return false;
}

bool updateProcStatuses(const ProcessVec *process_vec) {
    bool any_updated = false;
    for (size_t i = 0; i < process_vec->count; ++i) {
        Process *proc = &process_vec->items[i];
        if (proc->status == PROC_STATUS_TERMINATED) {
            continue;
        }

        ProcStatus proc_status = waitProcStatus(proc->pid);
        if (proc_status == PROC_STATUS_UNDEFINED) {
            LOG("%s(%i) returned PROC_STATUS_UNDEFINED\n", proc->name, proc->pid);
        }
        if (proc_status != PROC_STATUS_NOT_CHANGED) {
            proc->status = proc_status;
            any_updated = true;
        }
    }
    return any_updated;
}

ProcStatus getPipelineStatus(const ProcessVec *process_vec) {
    bool anyUndefined = false, anyRunning = false, anySuspended = false, anyTerminated = false;

    for (size_t i = 0; i < process_vec->count; ++i) {
        const Process *proc = &process_vec->items[i];
        switch (proc->status) {
        case PROC_STATUS_UNDEFINED:
            anyUndefined = true;
            break;

        case PROC_STATUS_SUSPENDED:
            anySuspended = true;
            break;

        case PROC_STATUS_RUNNING:
            anyRunning = true;
            break;

        case PROC_STATUS_TERMINATED:
            anyTerminated = true;
            break;
        case PROC_STATUS_NOT_CHANGED:
            LOG("process_vec contains process with invalid status (%s(%i))", proc->name, proc->pid);
            return PROC_STATUS_UNDEFINED;
        }
    }

    if (anyUndefined) {
        return PROC_STATUS_UNDEFINED;
    }
    if (anyRunning) {
        return PROC_STATUS_RUNNING;
    }
    if (anySuspended) {
        return PROC_STATUS_SUSPENDED;
    }
    if (anyTerminated) {
        return PROC_STATUS_TERMINATED;
    }

    LOG("unable to determine pipeline status\n");
    return PROC_STATUS_UNDEFINED;
}

Job copyJob(Job job) {
    Job copy = job;
    copy.pipeline = (ProcessVec){0};
    for (size_t i = 0; i < job.pipeline.count; ++i) {
        Process job_proc = job.pipeline.items[i];
        Process copy_proc = job_proc;
        copy_proc.name = cloneStr(job_proc.name);
        vecAppend(&copy.pipeline, copy_proc);
    }

    return copy;
}

bool indexOfPid(const ProcessVec *process_vec, pid_t pid, size_t *index) {
    for (size_t i = 0; i < process_vec->count; ++i) {
        if (process_vec->items[i].pid == pid) {
            *index = i;
            return true;
        }
    }
    return false;
}
