#include "dispatcher_utils.h"
#include "err.h"
#include "output_lock.h"
#include "utils.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define DEBUG 0

DispatcherLock dispatcherLock;

Task tasks[MAX_N_TASKS];

struct ListenerArgs {
    int task_id;
    int fd_id;
};

typedef struct ListenerArgs ListenerArgs;

void* output_listener(void* listenerArgs)
{
    ListenerArgs* ls = listenerArgs;

    Task* task = &tasks[ls->task_id];
    int k = ls->fd_id == STDOUT_FILENO ? 0 : 1;

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
        ASSERT_ZERO(pthread_mutex_lock(&task->mutex[k]));
        strcpy(task->output[k], bf);
        ASSERT_ZERO(pthread_mutex_unlock(&task->mutex[k]));
    }

    fclose(fs);
    free(bf);
    free(ls);

    return 0;
}

void* run(void* task_id)
{
    int t = *(int*)task_id;
    Task* task = &tasks[t];

    for (int k = 0; k < 2; k++)
        ASSERT_SYS_OK(pipe(task->pipefd[k]));

    ListenerArgs** args = malloc(2 * sizeof(ListenerArgs*));
    args[0] = malloc(sizeof(ListenerArgs));
    args[1] = malloc(sizeof(ListenerArgs));

    for (int k = 0; k < 2; k++) {
        args[k]->task_id = t;
        args[k]->fd_id = !k ? STDOUT_FILENO : STDERR_FILENO;
        ASSERT_ZERO(pthread_create(
            &task->thread[k + 1], NULL, output_listener, args[k]));
    }

    free(args);

    pid_t exec_pid;
    ASSERT_SYS_OK(exec_pid = fork());

    if (exec_pid == 0) {
        int output;
        for (int k = 0; k < 2; k++) {
            output = !k ? STDOUT_FILENO : STDERR_FILENO;
            ASSERT_SYS_OK(close(task->pipefd[k][0]));
            ASSERT_SYS_OK(dup2(task->pipefd[k][1], output));
            ASSERT_SYS_OK(close(task->pipefd[k][1]));
        }

        ASSERT_ZERO(execvp(task->args[1], task->args + 1));
    } else {
        for (int k = 0; k < 2; k++)
            ASSERT_SYS_OK(close(task->pipefd[k][1]));

        setpgid(exec_pid, exec_pid);
        task->exec_pid = exec_pid;

        printf("Task %d started: pid %d.\n", t, exec_pid);

        after_run(&dispatcherLock);

        int wstatus;
        waitpid(tasks[t].exec_pid, &wstatus, 0);

        before_status(&dispatcherLock);

        if (WIFSIGNALED(wstatus))
            printf("Task %d ended: signalled.\n", t);
        else
            printf("Task %d ended: status %d.\n", t, WEXITSTATUS(wstatus));

        after_status(&dispatcherLock);

        free_split_string(tasks[t].args);
    }

    return 0;
}

void run_dispatcher()
{
    bool quits = false;

    size_t buff_size = COMMAND_LENGTH;
    char* buff = malloc(buff_size * sizeof(char));

    if (!buff)
        syserr("malloc");

    char** parts;

    int next = 0;
    int arg;
    bool was_kill = false;
    bool run_cmd = false;

    while (!quits) {
        if (getline(&buff, &buff_size, stdin) < 0) {
            quits = true;
            kill_all(tasks, next);
            break;
        }

        before_dispatch(&dispatcherLock);

        if (was_kill) {
            after_kill(&dispatcherLock);
            was_kill = false;
        }

        remove_newline(buff);
        parts = split_string(buff);

        switch (get_cmd(parts[0])) {
        case RUN:
            run_cmd = true;
            tasks[next].args = parts;
            tasks[next].id = next;

            before_run(&dispatcherLock);
            ASSERT_ZERO(pthread_create(
                &tasks[next].thread[0], NULL, run, &tasks[next].id));
            next++;
            break;
        case KILL:
            if (DEBUG)
                printf("gotta kill\n");
            before_kill(&dispatcherLock);
            arg = (int)strtol(parts[1], NULL, 10);
            was_kill = true;
            ASSERT_SYS_OK(killpg(tasks[arg].exec_pid, SIGINT));
            break;
        case SLEEP:
            if (DEBUG)
                printf("gotta sleep\n");
            arg = (int)strtol(parts[1], NULL, 10);
            usleep(1000 * arg);
            break;
        case QUIT:
            quits = true;
            kill_all(tasks, next);
            break;
        case ERR:
            arg = (int)strtol(parts[1], NULL, 10);
            print_output(tasks, arg, STDERR);
            break;
        case OUT:
            arg = (int)strtol(parts[1], NULL, 10);
            print_output(tasks, arg, STDOUT);
            break;
        case EMPTY:
            break;
        }

        after_dispatch(&dispatcherLock);

        if (!run_cmd)
            // RUN passes parts deallocation to thread executing run()
            // otherwise, parts must be freed by the dispatcher.
            free_split_string(parts);
        run_cmd = false;
    }

    for (int i = 0; i < next; i++)
        for (int k = 0; k < 3; k++)
            ASSERT_ZERO(pthread_join(tasks[i].thread[k], NULL));

    free(buff);
}

int main()
{
    output_lock_init(&dispatcherLock);
    mutexes_init(tasks);

    run_dispatcher();

    mutexes_destroy(tasks);
    output_lock_destroy(&dispatcherLock);

    return 0;
}
