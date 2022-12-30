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

struct Task {
    pthread_t thread;
    char** args;
    char stdout[LINE_LENGTH];
    char stderr[LINE_LENGTH];
    sem_t mutex;
};

typedef struct Task Task;

struct SharedStorage {
    Task tasks[MAX_N_TASKS];
};

enum command { RUN, OUT, ERR, KILL, SLEEP, QUIT };

char* run_str = "run";
char* out_str = "out";
char* err_str = "err";
char* kill_str = "kill";
char* sleep_str = "sleep";

char mutex_names[MAX_N_TASKS][20];
const char* mutex_name = "/executor_mutex";

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
    int pipefd[2];
    Task* task = &s->tasks[t];

    ASSERT_SYS_OK(pipe(pipefd));

    pid_t pid = fork();
    ASSERT_SYS_OK(pid);

    if (pid == 0) {
        ASSERT_SYS_OK(close(pipefd[0]));

        ASSERT_SYS_OK(dup2(pipefd[1], STDOUT_FILENO));
        ASSERT_SYS_OK(close(pipefd[1]));

        ASSERT_ZERO(execvp(task->args[1], task->args + 1));
    } else {
        ASSERT_SYS_OK(close(pipefd[1]));

        FILE* fstream = fdopen(pipefd[0], "r");

        char* buff = calloc(LINE_LENGTH, sizeof(char));

        while (read_line(buff, LINE_LENGTH, fstream))
            if (buff[0] != '\0') {
                remove_newline(buff);
                ASSERT_SYS_OK(sem_wait(&task->mutex));
                strcpy(task->stdout, buff);
                ASSERT_SYS_OK(sem_post(&task->mutex));
            }

        free(buff);
        free_split_string(task->args);
    }
}

int main()
{
    size_t buffer_size = COMMAND_LENGTH;
    char* buffer = malloc(buffer_size * sizeof(char));
    char** parts;

    if (!buffer)
        syserr("malloc");

    struct SharedStorage* shared_storage
        = mmap(NULL, sizeof(struct SharedStorage), PROT_READ | PROT_WRITE,
            MAP_SHARED | MAP_ANONYMOUS, -1, 0);

    for (int i = 0; i < MAX_N_TASKS; i++) {
        char num[3];
        sprintf(num, "%d", i);
        strcat(mutex_names[i], mutex_name);
        strcat(mutex_names[i], num);

        shared_storage->tasks[i].mutex
            = *sem_open(mutex_names[i], O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, 1);
    }

    if (shared_storage == MAP_FAILED)
        syserr("mmap");

    bool quits = false;
    int next_task = 0;

    pid_t pid;
    int t;

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
        case OUT:
            t = strtol(parts[1], NULL, 10);
            ASSERT_SYS_OK(sem_wait(&shared_storage->tasks[t].mutex));
            printf("Task %d stdout: \'%s\'.\n", t,
                shared_storage->tasks[t].stdout);
            ASSERT_SYS_OK(sem_post(&shared_storage->tasks[t].mutex));
            free_split_string(parts);
            break;
        case ERR:
            free_split_string(parts);
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
        }
    }

    // Temporary measure
    for (int i = 0; i < next_task - 1; i++)
        ASSERT_SYS_OK(pthread_join(shared_storage->tasks[i].thread, NULL));

    for (int i = 0; i < MAX_N_TASKS; i++)
        ASSERT_SYS_OK(sem_unlink(mutex_names[i]));
    // After unlink the OS will reclaim semaphore's resources once its reference
    // count drops to zero.

    ASSERT_SYS_OK(munmap((void*)shared_storage, sizeof(struct SharedStorage)));

    free(buffer);

    return 0;
}
