#pragma once
#include <sys/types.h>
#include <termios.h>
#include <stdbool.h>

typedef enum ProcStatus {
    PROC_STATUS_UNDEFINED,
    PROC_STATUS_TERMINATED,
    PROC_STATUS_SUSPENDED,
    PROC_STATUS_RUNNING,
    PROC_STATUS_NOT_CHANGED,
} ProcStatus;

typedef struct {
    pid_t pid;
    const char *name;
    ProcStatus status;
} Process;

typedef struct {
    Process *items;
    size_t count;
    size_t capacity;
} ProcessVec;

typedef struct {
    ProcessVec pipeline;
    pid_t pgid;
    struct termios termios_conf;
} Job;

typedef struct {
    Job *items;
    size_t count;
    size_t capacity;
} JobsVec;


void freeProcessFields(const Process *process);
void printProcess(const Process *proc);
void printJob(const Job *job);
void printWStatusDesc(int wstatus, const char *procname, pid_t pid);
ProcStatus waitProcStatus(pid_t pid);
bool anyProcessStatus(const ProcessVec *process_vec, ProcStatus status);
bool updateProcStatuses(const ProcessVec *process_vec);
ProcStatus getPipelineStatus(const ProcessVec *process_vec);
Job copyJob(Job job);
bool indexOfPid(const ProcessVec *process_vec, pid_t pid, size_t *index);
