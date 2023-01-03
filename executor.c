#include "err.h"
#include "utils.h"
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define COMMAND_LENGTH 511
#define LINE_LENGTH 1022
#define MAX_N_TASKS 4096
#define STDOUT 0
#define STDERR 1

#define DEBUG 0

struct Task {
    // thread 0 runs the task while threads 1,2 listen for stdout/err
    // thread 4 finishes
    pthread_t thread[4];
    pid_t exec_pid;

    int id;
    char** args;
    char output[2][LINE_LENGTH];
    int pipefd[2][2];
    pthread_mutex_t mutex[2];
};

typedef struct Task Task;

Task tasks[MAX_N_TASKS];

enum cmd { RUN, OUT, ERR, KILL, SLEEP, QUIT, EMPTY };

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
    if (DEBUG) printf("%d got %s\n", getpid(), str);
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

// takes arg array as argument. arg[0] - task number, arg[1] -
// listened fd
void* output_listener(void* p)
{
    int* s = p;

    Task* task = &tasks[s[0]];
    int k = s[1] == STDOUT_FILENO;

    int fd = task->pipefd[k][0];

    FILE* fs = fdopen(fd, "r");
    if (!fs)
        syserr("fdopen");

    size_t buff_size = LINE_LENGTH;

    char* bf = calloc(buff_size, sizeof(char));
    if (!bf)
        syserr("calloc");

    ssize_t read;

    while ((read = getline(&bf, &buff_size, fs)) && read > 0) {
        remove_newline(bf);
        if (DEBUG)
            printf("got \'%s\', k=%d\n", bf, k);
        ASSERT_ZERO(pthread_mutex_lock(&task->mutex[k]));
        strcpy(task->output[k], bf);
        ASSERT_ZERO(pthread_mutex_unlock(&task->mutex[k]));
    }

    if (DEBUG)
        printf("Left loop\n");

    fclose(fs);
    free(bf);

    return 0;
}

void* run(void* p)
{
    int t = *(int*)p;
    Task* task = &tasks[t];

    for (int k = 0; k < 2; k++)
        ASSERT_SYS_OK(pipe(task->pipefd[k]));

    int args[2][2] = { { t, STDOUT_FILENO }, { t, STDERR_FILENO } };

    for (int k = 0; k < 2; k++)
        ASSERT_ZERO(pthread_create(
            &task->thread[k + 1], NULL, output_listener, &args[k]));

    pid_t task_pid;
    ASSERT_SYS_OK(task_pid = fork());

    if (task_pid == 0) {
        int output;
        for (int k = 0; k < 2; k++) {
            output = !k ? STDOUT_FILENO : STDERR_FILENO;
            ASSERT_SYS_OK(close(task->pipefd[k][0]));
            ASSERT_SYS_OK(dup2(task->pipefd[k][1], output));
            ASSERT_SYS_OK(close(task->pipefd[k][1]));
        }
        setpgid(0, 0);
        ASSERT_ZERO(execvp(task->args[1], task->args + 1));
    } else {
        for (int k = 0; k < 2; k++)
            ASSERT_SYS_OK(close(task->pipefd[k][1]));
        task->exec_pid = task_pid;
        printf("Task %d started: pid %d.\n", t, task_pid);

        int wstatus;
        waitpid(tasks[t].exec_pid, &wstatus, 0);

        if (WIFSIGNALED(wstatus))
            printf("Task %d ended: signalled.\n", t);
        else
            printf("Task %d ended: status %d.\n", t, wstatus);

        free_split_string(tasks[t].args);
    }

    return 0;
}

void mutexes_init()
{
    for (int i = 0; i < MAX_N_TASKS; i++)
        for (int k = 0; k < 2; k++)
            ASSERT_ZERO(pthread_mutex_init(&tasks[i].mutex[k], NULL));
}

void mutexes_destroy()
{
    for (int i = 0; i < MAX_N_TASKS; i++)
        for (int k = 0; k < 2; k++)
            ASSERT_ZERO(pthread_mutex_destroy(&tasks[i].mutex[k]));
}

void print_output(int t, int k)
{
    ASSERT_ZERO(pthread_mutex_lock(&tasks[t].mutex[k]));
    printf("Task %d %s: \'%s\'.\n", t, output_str[k], tasks[t].output[k]);
    ASSERT_ZERO(pthread_mutex_unlock(&tasks[t].mutex[k]));
}

const int buffer_size = COMMAND_LENGTH;

void cmd_dispatcher()
{
    bool quits = false;
    char* buffer = malloc(buffer_size * sizeof(char));

    if (!buffer)
        syserr("malloc");

    char** parts;

    int next = 0;
    int task_id, time;

    while (!quits) {
        if (!read_line(buffer, buffer_size, stdin)) {
            quits = true;
            for (int id = 0; id < next; id++)
                ASSERT_SYS_OK(killpg(tasks[id].exec_pid, SIGINT));
            break;
        }

        remove_newline(buffer);
        parts = split_string(buffer);

        switch (get_cmd(parts[0])) {
        case RUN:
            tasks[next].args = parts;
            tasks[next].id = next;
            ASSERT_ZERO(pthread_create(
                &tasks[next].thread[0], NULL, run, &tasks[next].id));
            next++;
            break;
        case KILL:
            task_id = strtol(parts[1], NULL, 10);
            if (DEBUG) printf("sigint %d\n", tasks[task_id].exec_pid);
            ASSERT_SYS_OK(killpg(tasks[task_id].exec_pid, SIGINT));
            break;
        case SLEEP:
            time = strtol(parts[1], NULL, 10);
            usleep(1000 * time);
            free_split_string(parts);
            break;
        case QUIT:
            quits = true;
            for (int id = 0; id < next; id++)
                ASSERT_SYS_OK(killpg(tasks[id].exec_pid, SIGINT));
            free_split_string(parts);
            break;
        case ERR:
            task_id = strtol(parts[1], NULL, 10);
            print_output(task_id, STDERR);
            free_split_string(parts);
            break;
        case OUT:
            task_id = strtol(parts[1], NULL, 10);
            print_output(task_id, STDOUT);
            free_split_string(parts);
            break;
        case EMPTY:
            free_split_string(parts);
            break;
        }
    }

    for (int i = 0; i < next - 1; i++)
        for (int k = 0; k < 3; k++)
            ASSERT_ZERO(pthread_join(tasks[i].thread[k], NULL));

    free(buffer);
}

int main()
{
    mutexes_init();

    cmd_dispatcher();

    mutexes_destroy();

    return 0;
}
