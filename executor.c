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
    pid_t pid;
    pthread_t thread;
    char** args;
    char output[2][LINE_LENGTH];
    sem_t mutex[2];
};

typedef struct Task Task;

struct SharedStorage {
    Task tasks[MAX_N_TASKS];
};

enum cmd { RUN, OUT, ERR, KILL, SLEEP, QUIT };

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

    struct SharedStorage* s = mmap(NULL, sizeof(struct SharedStorage),
        PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);

    if (s == MAP_FAILED)
        syserr("mmap");

    mutexes_init(s);

    bool quits = false;
    int next = 0;

    pid_t task_pid;

    while (!quits) {
        if (!read_line(buffer, buffer_size, stdin)) {
            quits = true;
            break;
        }

        remove_newline(buffer);
        parts = split_string(buffer);

        switch (get_cmd(parts[0])) {
        case RUN:
            s->tasks[next].args = parts;
            if ((task_pid = fork()) < 0)
                exit(1);
            else if (task_pid == 0) {
                run(next, s);
                return 0;
            } else {
                printf("Task %d started: task_pid %d\n", next, task_pid);
                s->tasks[next].pid = task_pid;
                next++;
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
            print_output(strtol(parts[1], NULL, 10), STDERR, s);
            free_split_string(parts);
            break;
        case OUT:
            print_output(strtol(parts[1], NULL, 10), STDOUT, s);
            free_split_string(parts);
            break;
        }
    }

    // Temporary measure
    for (int i = 0; i < next - 1; i++)
        ASSERT_SYS_OK(pthread_join(s->tasks[i].thread, NULL));

    for (int i = 0; i < MAX_N_TASKS; i++)
        for (int k = 0; k < 2; k++)
            ASSERT_SYS_OK(sem_unlink(mutex_names[k][i]));

    // After unlink the OS will reclaim semaphore's resources once its reference
    // count drops to zero.

    ASSERT_SYS_OK(munmap((void*)s, sizeof(struct SharedStorage)));

    free(buffer);

    return 0;
}
