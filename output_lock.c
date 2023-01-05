#include "output_lock.h"
#include "err.h"
#include <stdio.h>

#define DEBUG 0

void output_lock_init(DispatcherLock* dl)
{
    ASSERT_ZERO(pthread_mutex_init(&dl->mutex, NULL));
    ASSERT_ZERO(pthread_cond_init(&dl->dispatcher, NULL));
    ASSERT_ZERO(pthread_cond_init(&dl->ended_tasks, NULL));
    dl->ended_tasks_waiting = 0;
    dl->task_run_steps = 0;
    dl->ended_tasks_outputting = 0;
    dl->dispatcher_running = true;
    dl->ended_tasks_priority = false;
}

void output_lock_destroy(DispatcherLock* dl)
{
    ASSERT_ZERO(pthread_mutex_destroy(&dl->mutex));
    ASSERT_ZERO(pthread_cond_destroy(&dl->dispatcher));
    ASSERT_ZERO(pthread_cond_destroy(&dl->ended_tasks));
}

// assumes no ended_tasks waiting
void before_run(DispatcherLock* dl)
{
    ASSERT_ZERO(pthread_mutex_lock(&dl->mutex));

    dl->task_run_steps++;

    ASSERT_ZERO(pthread_mutex_unlock(&dl->mutex));
}

// assumes no ended_tasks waiting
void after_run(DispatcherLock* dl)
{
    ASSERT_ZERO(pthread_mutex_lock(&dl->mutex));

    dl->task_run_steps--;

    if (dl->task_run_steps == 0) {
        if (dl->ended_tasks_waiting > 0)
            ASSERT_ZERO(pthread_cond_signal(&dl->ended_tasks));
        else if (!dl->ended_tasks_priority && dl->ended_tasks_outputting == 0)
            ASSERT_ZERO(pthread_cond_signal(&dl->dispatcher));
    }

    ASSERT_ZERO(pthread_mutex_unlock(&dl->mutex));
}

void before_status(DispatcherLock* dl)
{
    ASSERT_ZERO(pthread_mutex_lock(&dl->mutex));

    while (!dl->ended_tasks_priority && dl->dispatcher_running) {
        dl->ended_tasks_waiting++;
        if (DEBUG)
            printf("before_status waiting %d\n", dl->ended_tasks_waiting);
        ASSERT_ZERO(pthread_cond_wait(&dl->ended_tasks, &dl->mutex));
        dl->ended_tasks_waiting--;
        if (DEBUG)
            printf("before_status leaving loop. waiting left %d\n",
                dl->ended_tasks_waiting);
    }

    dl->ended_tasks_outputting++;

    ASSERT_ZERO(pthread_mutex_unlock(&dl->mutex));
}

void before_kill(DispatcherLock* dl)
{
    ASSERT_ZERO(pthread_mutex_lock(&dl->mutex));

    dl->ended_tasks_priority = true;

    ASSERT_ZERO(pthread_mutex_unlock(&dl->mutex));
}

void after_kill(DispatcherLock* dl)
{
    ASSERT_ZERO(pthread_mutex_lock(&dl->mutex));

    dl->ended_tasks_priority = false;

    ASSERT_ZERO(pthread_mutex_unlock(&dl->mutex));
}

void after_status(DispatcherLock* dl)
{
    ASSERT_ZERO(pthread_mutex_lock(&dl->mutex));

    dl->ended_tasks_outputting--;

    if (dl->ended_tasks_waiting > 0)
        ASSERT_ZERO(pthread_cond_signal(&dl->ended_tasks));
    else if (dl->ended_tasks_outputting + dl->task_run_steps == 0) {
        dl->ended_tasks_priority = false;
        ASSERT_ZERO(pthread_cond_signal(&dl->dispatcher));
    }

    ASSERT_ZERO(pthread_mutex_unlock(&dl->mutex));
}

void before_dispatch(DispatcherLock* dl)
{
    ASSERT_ZERO(pthread_mutex_lock(&dl->mutex));

    if (DEBUG)
        printf("before_dispatch\n");

    while (dl->ended_tasks_priority
        || dl->ended_tasks_waiting + dl->ended_tasks_outputting
                + dl->task_run_steps
            > 0) {
        if (DEBUG)
            printf("dispatch waiting\n");
        ASSERT_ZERO(pthread_cond_wait(&dl->dispatcher, &dl->mutex));
        if (DEBUG)
            printf("dispatch leaving loop\n");
    }

    dl->dispatcher_running = true;

    ASSERT_ZERO(pthread_mutex_unlock(&dl->mutex));
}

void after_dispatch(DispatcherLock* dl)
{
    ASSERT_ZERO(pthread_mutex_lock(&dl->mutex));

    dl->dispatcher_running = false;

    if (DEBUG)
        printf("after_dispatch\n");

    ASSERT_ZERO(pthread_cond_signal(&dl->ended_tasks));

    ASSERT_ZERO(pthread_mutex_unlock(&dl->mutex));
}
