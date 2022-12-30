#include "err.h"
#include "utils.h"
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define COMMAND_LENGTH 511
#define LINE_LENGTH 1022
#define MAX_N_TASKS 4096
#define STDOUT 0
#define STDERR 1

struct Task {
    pthread_t thread;
    char** args;
    char output[2][LINE_LENGTH];
    sem_t mutex[2];
};

typedef struct Task Task;

struct SharedStorage {
    Task tasks[MAX_N_TASKS];
};

enum command { RUN, OUT, ERR, KILL, SLEEP, QUIT };

const char* run_str = "run";
const char* out_str = "out";
const char* err_str = "err";
const char* kill_str = "kill";
const char* sleep_str = "sleep";
const char* output_str[2] = { "stdout", "stderr" };
const char* mutex_name[2] = { "/executor_mutex_o", "/executor_mutex_e" };

char mutex_names[2][MAX_N_TASKS][22];

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

void run(int t, struct SharedStorage* s)
{
    int pipefd[2][2];
    Task* task = &s->tasks[t];

    for (int k = 0; k < 2; k++)
        ASSERT_SYS_OK(pipe(pipefd[k]));

    pid_t exec_pid = fork();
    ASSERT_SYS_OK(exec_pid);

    if (exec_pid == 0) {
        for (int k = 0; k < 2; k++) {
            ASSERT_SYS_OK(close(pipefd[k][0]));
            ASSERT_SYS_OK(
                dup2(pipefd[k][1], !k ? STDOUT_FILENO : STDERR_FILENO));
            ASSERT_SYS_OK(close(pipefd[k][1]));
        }

        ASSERT_ZERO(execvp(task->args[1], task->args + 1));
    } else {
        pid_t stdout_pid = fork();
        ASSERT_SYS_OK(stdout_pid);

        int k = !stdout_pid ? 1 : 0;
        // Split into processes with k = 0, 1, which handle
        // immediate stdout, stderr read, respectively.

        ASSERT_SYS_OK(close(pipefd[k][1]));

        FILE* fstream = fdopen(pipefd[k][0], "r");
        if (!fstream)
            syserr("fdopen");

        char* buff = calloc(LINE_LENGTH, sizeof(char));
        if (!buff)
            syserr("calloc");

        while (read_line(buff, LINE_LENGTH, fstream))
            if (buff[0] != '\0') {
                remove_newline(buff);
                ASSERT_SYS_OK(sem_wait(&task->mutex[k]));
                strcpy(task->output[k], buff);
                ASSERT_SYS_OK(sem_post(&task->mutex[k]));
            }

        free(buff);
        free_split_string(task->args);
    }
}

void mutexes_init(struct SharedStorage* s)
{
    for (int i = 0; i < MAX_N_TASKS; i++) {
        char num[4];
        sprintf(num, "%d", i);
        for (int k = 0; k < 2; k++) {
            strcat(mutex_names[k][i], mutex_name[k]);
            strcat(mutex_names[k][i], num);

            sem_t* sem = sem_open(mutex_names[k][i], O_CREAT | O_RDWR | O_EXCL,
                S_IRUSR | S_IWUSR, 1);

            if (sem == SEM_FAILED)
                syserr("sem_open");

            s->tasks[i].mutex[k] = *sem;
        }
    }
}

void print_output(int t, int k, struct SharedStorage* s)
{
    ASSERT_SYS_OK(sem_wait(&s->tasks[t].mutex[k]));
    printf("Task %d %s: \'%s\'.\n", t, output_str[k], s->tasks[t].output[k]);
    ASSERT_SYS_OK(sem_post(&s->tasks[t].mutex[k]));
}

int main()
{
    int buffer_size = COMMAND_LENGTH;
    char* buffer = malloc(buffer_size * sizeof(char));
    char** parts;

    if (!buffer)
        syserr("malloc");

    struct SharedStorage* shared_storage
        = mmap(NULL, sizeof(struct SharedStorage), PROT_READ | PROT_WRITE,
            MAP_SHARED | MAP_ANONYMOUS, -1, 0);

    if (shared_storage == MAP_FAILED)
        syserr("mmap");

    mutexes_init(shared_storage);

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
            shared_storage->tasks[next_task].args = parts;
            if ((pid = fork()) < 0)
                exit(1);
            else if (pid == 0) {
                run(next_task, shared_storage);
                return 0;
            } else {
                printf("Task %d started: pid %d\n", next_task, pid);
                next_task++;
            }
            break;
        case KILL:
            free_split_string(parts);
            break;
        case SLEEP:
            usleep(strtol(parts[1], NULL, 10));
            free_split_string(parts);
            break;
        case QUIT:
            quits = true;
            free_split_string(parts);
            break;
        case ERR:
            print_output(strtol(parts[1], NULL, 10), STDERR, shared_storage);
            free_split_string(parts);
            break;
        case OUT:
            print_output(strtol(parts[1], NULL, 10), STDOUT, shared_storage);
            free_split_string(parts);
            break;
        }
    }

    // Temporary measure
    for (int i = 0; i < next_task - 1; i++)
        ASSERT_SYS_OK(pthread_join(shared_storage->tasks[i].thread, NULL));

    for (int i = 0; i < MAX_N_TASKS; i++)
        for (int k = 0; k < 2; k++)
            ASSERT_SYS_OK(sem_unlink(mutex_names[k][i]));

    // After unlink the OS will reclaim semaphore's resources once its reference
    // count drops to zero.

    ASSERT_SYS_OK(munmap((void*)shared_storage, sizeof(struct SharedStorage)));

    free(buffer);

    return 0;
}
