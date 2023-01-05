#ifndef MIMUW_FORK_DISPATCHER_UTILS_H
#define MIMUW_FORK_DISPATCHER_UTILS_H

#include <pthread.h>

#define COMMAND_LENGTH 511
#define LINE_LENGTH 1022
#define MAX_N_TASKS 4096
#define STDOUT 0
#define STDERR 1

struct Task {
    // Thread 0 forks to execute task specified in args in a process with
    // exec_pid; threads 1 and 2 listen for its STDOUT/ERR; thread 4 waits for
    // the task to end.
    pthread_t thread[4];
    pid_t exec_pid;

    int id; // task id assigned by the dispatcher
    char** args; // args[0] - program to execute, args + 1 - program's arguments

    // Size-2 arrays below facilitate output gathering by listener threads.
    // index 0 refers to STDOUT, index 1 to STDERR
    char output[2][LINE_LENGTH]; // Buffers for last STDOUT/ERR lines.
    pthread_mutex_t mutex[2]; // Mutexes for STDOUT/ERR output[] buffers.

    int pipefd[2][2]; // Two pipes that connect exec_pid process' output with
                      // listener threads.
};

typedef struct Task Task;

enum cmd { RUN, OUT, ERR, KILL, SLEEP, QUIT, EMPTY };

enum cmd get_cmd(char* str);

void remove_newline(char* str);

void kill_all(Task tasks[], int next_id);

void mutexes_init(Task tasks[]);

void mutexes_destroy(Task tasks[]);

void print_output(Task tasks[], int t, int k);

#endif // MIMUW_FORK_DISPATCHER_UTILS_H
