#include "output_lock.h"
#include "err.h"
#include <stdio.h>

void output_lock_init(DispatcherLock* dl)
{
    ASSERT_ZERO(pthread_mutex_init(&dl->mutex, NULL));
    ASSERT_ZERO(pthread_cond_init(&dl->dispatcher, NULL));
    ASSERT_ZERO(pthread_cond_init(&dl->ended_tasks, NULL));
    dl->ended_tasks_waiting = 0;
    dl->task_run_steps = 0;
    dl->ended_tasks_outputting = 0;
    dl->dispatcher_running = true;
}

void output_lock_destroy(DispatcherLock* dl)
{
    ASSERT_ZERO(pthread_mutex_destroy(&dl->mutex));
    ASSERT_ZERO(pthread_cond_destroy(&dl->dispatcher));
    ASSERT_ZERO(pthread_cond_destroy(&dl->ended_tasks));
}

// assumes no ended_tasks waiting
void before_run(DispatcherLock* dl) {
    ASSERT_ZERO(pthread_mutex_lock(&dl->mutex));

    dl->task_run_steps++;

    ASSERT_ZERO(pthread_mutex_unlock(&dl->mutex));
}

// assumes no ended_tasks waiting
void after_run(DispatcherLock* dl) {
    ASSERT_ZERO(pthread_mutex_lock(&dl->mutex));

    dl->task_run_steps--;

    if (dl->task_run_steps == 0) {
        if (dl->ended_tasks_waiting > 0)
            ASSERT_ZERO(pthread_cond_signal(&dl->ended_tasks));
        else if (dl->ended_tasks_outputting == 0)
            ASSERT_ZERO(pthread_cond_signal(&dl->dispatcher));
    }

    ASSERT_ZERO(pthread_mutex_unlock(&dl->mutex));
}

void before_output(DispatcherLock* dl)
{
    ASSERT_ZERO(pthread_mutex_lock(&dl->mutex));

    while (dl->dispatcher_running) {
        dl->ended_tasks_waiting++;
        ASSERT_ZERO(pthread_cond_wait(&dl->ended_tasks, &dl->mutex));
        dl->ended_tasks_waiting--;
    }

    dl->ended_tasks_outputting++;

    ASSERT_ZERO(pthread_mutex_unlock(&dl->mutex));
}

void after_output(DispatcherLock* dl)
{
    ASSERT_ZERO(pthread_mutex_lock(&dl->mutex));

    dl->ended_tasks_outputting--;

    if (dl->ended_tasks_waiting > 0)
        ASSERT_ZERO(pthread_cond_signal(&dl->ended_tasks));
    else if (dl->ended_tasks_outputting + dl->task_run_steps == 0)
        ASSERT_ZERO(pthread_cond_signal(&dl->dispatcher));

    ASSERT_ZERO(pthread_mutex_unlock(&dl->mutex));
}

void before_dispatch(DispatcherLock* dl)
{
    ASSERT_ZERO(pthread_mutex_lock(&dl->mutex));

    while (dl->ended_tasks_waiting + dl->ended_tasks_outputting
            + dl->task_run_steps > 0)
        ASSERT_ZERO(pthread_cond_wait(&dl->dispatcher, &dl->mutex));

    dl->dispatcher_running = true;

    ASSERT_ZERO(pthread_mutex_unlock(&dl->mutex));
}

void after_dispatch(DispatcherLock* dl)
{
    ASSERT_ZERO(pthread_mutex_lock(&dl->mutex));

    dl->dispatcher_running = false;

    ASSERT_ZERO(pthread_cond_signal(&dl->ended_tasks));

    ASSERT_ZERO(pthread_mutex_unlock(&dl->mutex));
}
