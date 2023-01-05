#ifndef MIMUW_FORK_OUTPUT_LOCK_H
#define MIMUW_FORK_OUTPUT_LOCK_H

#include <pthread.h>
#include <stdbool.h>

struct OutputLock {
    pthread_mutex_t mutex;
    pthread_cond_t dispatcher;
    pthread_cond_t ended_tasks;
    bool dispatcher_running;
    int ended_tasks_waiting;
    int ended_tasks_outputting;
};

typedef struct OutputLock OutputLock;

void output_lock_init(OutputLock* ol);

void output_lock_destroy(OutputLock* ol);

void before_output(OutputLock* ol);

void after_output(OutputLock* ol);

void before_dispatch(OutputLock* ol);

void after_dispatch(OutputLock* ol);

#endif // MIMUW_FORK_OUTPUT_LOCK_H
