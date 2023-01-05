#include "output_lock.h"
#include "err.h"

void output_lock_init(OutputLock* ol)
{
    ASSERT_ZERO(pthread_mutex_init(&ol->mutex, NULL));
    ASSERT_ZERO(pthread_cond_init(&ol->dispatcher, NULL));
    ASSERT_ZERO(pthread_cond_init(&ol->ended_tasks, NULL));
    ol->ended_tasks_waiting = 0;
    ol->ended_tasks_outputting = 0;
    ol->dispatcher_running = true;
}

void output_lock_destroy(OutputLock* ol)
{
    ASSERT_ZERO(pthread_mutex_destroy(&ol->mutex));
    ASSERT_ZERO(pthread_cond_destroy(&ol->dispatcher));
    ASSERT_ZERO(pthread_cond_destroy(&ol->ended_tasks));
}

void before_output(OutputLock* ol)
{
    ASSERT_ZERO(pthread_mutex_lock(&ol->mutex));

    while (ol->dispatcher_running) {
        ol->ended_tasks_waiting++;
        ASSERT_ZERO(pthread_cond_wait(&ol->ended_tasks, &ol->mutex));
        ol->ended_tasks_waiting--;
    }

    ol->ended_tasks_outputting++;

    ASSERT_ZERO(pthread_mutex_unlock(&ol->mutex));
}

void after_output(OutputLock* ol)
{
    ASSERT_ZERO(pthread_mutex_lock(&ol->mutex));

    ol->ended_tasks_outputting--;

    if (ol->ended_tasks_waiting > 0)
        ASSERT_ZERO(pthread_cond_signal(&ol->ended_tasks));
    else if (ol->ended_tasks_outputting == 0)
        ASSERT_ZERO(pthread_cond_signal(&ol->dispatcher));

    ASSERT_ZERO(pthread_mutex_unlock(&ol->mutex));
}

void before_dispatch(OutputLock* ol)
{
    ASSERT_ZERO(pthread_mutex_lock(&ol->mutex));

    while (ol->ended_tasks_waiting + ol->ended_tasks_outputting > 0)
        ASSERT_ZERO(pthread_cond_wait(&ol->dispatcher, &ol->mutex));

    ol->dispatcher_running = true;

    ASSERT_ZERO(pthread_mutex_unlock(&ol->mutex));
}

void after_dispatch(OutputLock* ol)
{
    ASSERT_ZERO(pthread_mutex_lock(&ol->mutex));

    ol->dispatcher_running = false;

    ASSERT_ZERO(pthread_cond_signal(&ol->ended_tasks));

    ASSERT_ZERO(pthread_mutex_unlock(&ol->mutex));
}
