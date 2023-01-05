#include "dispatcher_utils.h"
#include "err.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

const char* run_str = "run";
const char* out_str = "out";
const char* err_str = "err";
const char* kill_str = "kill";
const char* sleep_str = "sleep";
const char* output_str[2] = { "stdout", "stderr" };

void remove_newline(char* str)
{
    char* ptr = strstr(str, "\n");
    if (ptr != NULL)
        strncpy(ptr, "\0", 1);
}

enum cmd get_cmd(char* str)
{
    if (strcmp(str, run_str) == 0)
        return RUN;
    if (strcmp(str, out_str) == 0)
        return OUT;
    if (strcmp(str, err_str) == 0)
        return ERR;
    if (strcmp(str, kill_str) == 0)
        return KILL;
    if (strcmp(str, sleep_str) == 0)
        return SLEEP;
    if (strcmp(str, "") == 0)
        return EMPTY;
    return QUIT;
}

void interrupt_task(Task tasks[], int id) {
    killpg(tasks[id].exec_pid, SIGINT);
}

void kill_all(Task tasks[], int next_id)
{
    for (int id = 0; id < next_id; id++)
        killpg(tasks[id].exec_pid, SIGKILL);
}

void mutexes_init(Task tasks[])
{
    for (int i = 0; i < MAX_N_TASKS; i++)
        for (int k = 0; k < 2; k++)
            ASSERT_ZERO(pthread_mutex_init(&tasks[i].mutex[k], NULL));
}

void mutexes_destroy(Task tasks[])
{
    for (int i = 0; i < MAX_N_TASKS; i++)
        for (int k = 0; k < 2; k++)
            ASSERT_ZERO(pthread_mutex_destroy(&tasks[i].mutex[k]));
}

void print_output(Task tasks[], int t, int k)
{
    ASSERT_ZERO(pthread_mutex_lock(&tasks[t].mutex[k]));
    printf("Task %d %s: \'%s\'.\n", t, output_str[k], tasks[t].output[k]);
    ASSERT_ZERO(pthread_mutex_unlock(&tasks[t].mutex[k]));
}
