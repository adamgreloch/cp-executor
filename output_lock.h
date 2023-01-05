#ifndef MIMUW_FORK_OUTPUT_LOCK_H
#define MIMUW_FORK_OUTPUT_LOCK_H

#include <pthread.h>
#include <stdbool.h>

struct DispatcherLock {
    pthread_mutex_t mutex;
    pthread_cond_t dispatcher;
    pthread_cond_t ended_tasks;
    bool dispatcher_running;
    int task_run_steps;
    int ended_tasks_waiting;
    int ended_tasks_outputting;
    bool ended_tasks_priority;
};

typedef struct DispatcherLock DispatcherLock;

void before_run(DispatcherLock* dl);

void after_run(DispatcherLock* dl);

void output_lock_init(DispatcherLock* dl);

void output_lock_destroy(DispatcherLock* dl);

void before_kill(DispatcherLock* dl);

void after_kill(DispatcherLock* dl);

void before_status(DispatcherLock* dl);

void after_status(DispatcherLock* dl);

void before_dispatch(DispatcherLock* dl);

void after_dispatch(DispatcherLock* dl);

#endif // MIMUW_FORK_OUTPUT_LOCK_H
