#include "err.h"
#include "utils.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define COMMAND_LENGTH 511
#define LINE_LENGTH 1022
#define MAX_N_TASKS 4096

struct Task {
    pthread_t thread;
    char** args;
    char* stdout[LINE_LENGTH];
    char* stderr[LINE_LENGTH];
};

typedef struct Task Task;

Task tasks[MAX_N_TASKS];

enum command {
    RUN,
    OUT,
    ERR,
    KILL,
    SLEEP,
    QUIT
};

char* run_str = "run";
char* out_str = "out";
char* err_str = "err";
char* kill_str = "kill";
char* sleep_str = "sleep";

void remove_newline(char* str)
{
    char* ptr = strstr(str, "\n");
    if (ptr != NULL)
        strncpy(ptr, "\0", 1);
}

enum command get_command(char* str)
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
    return QUIT;
}

void run(Task* t)
{
    execvp(t->args[1], t->args + 1);
    free_split_string(t->args);
}

int main()
{
    size_t buffer_size = COMMAND_LENGTH;
    char* buffer = malloc(buffer_size * sizeof(char));
    char** parts;

    if (!buffer) {
        perror("Failed to allocate input buffer");
        exit(1);
    }

    bool quits = false;
    int next_task = 0;

    pid_t pid;

    while (!quits) {
        if (!read_line(buffer, buffer_size, stdin)) {
            quits = true;
            break;
        }

        remove_newline(buffer);

        parts = split_string(buffer);

        switch (get_command(parts[0])) {
        case RUN:
            tasks[next_task].args = parts;
            if ((pid = fork()) < 0)
                exit(1);
            else if (pid == 0) {
                run(&tasks[next_task]);
                return 0;
            } else {
                printf("Task %d started: pid %d\n", next_task, pid);
                next_task++;
            }
            break;
        case OUT:
            break;
        case ERR:
            break;
        case KILL:
            break;
        case SLEEP:
            usleep(strtol(parts[1], NULL, 10));
            break;
        case QUIT:
            quits = true;
            break;
        }
    }

    for (size_t i = 0; i < next_task - 1; i++)
        ASSERT_SYS_OK(pthread_join(tasks[i].thread, NULL));

    free(buffer);

    return 0;
}
