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

DispatcherLock dispatcherLock;

Task tasks[MAX_N_TASKS];

// Takes int array[2] as an argument.
// arg[0] - task number, arg[1] - listened fd id
void* output_listener(void* int_arr)
{
    int* s = int_arr;

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
        ASSERT_ZERO(pthread_mutex_lock(&task->mutex[k]));
        strcpy(task->output[k], bf);
        ASSERT_ZERO(pthread_mutex_unlock(&task->mutex[k]));
    }

    fclose(fs);
    free(bf);

    return 0;
}

void* run(void* task_id)
{
    int t = *(int*)task_id;
    Task* task = &tasks[t];

    for (int k = 0; k < 2; k++)
        ASSERT_SYS_OK(pipe(task->pipefd[k]));

    int args[2][2] = { { t, STDOUT_FILENO }, { t, STDERR_FILENO } };

    for (int k = 0; k < 2; k++)
        ASSERT_ZERO(pthread_create(
            &task->thread[k + 1], NULL, output_listener, &args[k]));

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

        before_output(&dispatcherLock);

        if (WIFSIGNALED(wstatus))
            printf("Task %d ended: signalled.\n", t);
        else
            printf("Task %d ended: status %d.\n", t, WEXITSTATUS(wstatus));

        after_output(&dispatcherLock);

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
    bool run_cmd = false;

    while (!quits) {
        if (!read_line(buff, buff_size, stdin)) {
            quits = true;
            kill_all(tasks, next);
            break;
        }

        before_dispatch(&dispatcherLock);

        remove_newline(buff);
        parts = split_string(buff);
        arg = (int)strtol(parts[1], NULL, 10);

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
            ASSERT_SYS_OK(killpg(tasks[arg].exec_pid, SIGINT));
            break;
        case SLEEP:
            usleep(1000 * arg);
            break;
        case QUIT:
            quits = true;
            kill_all(tasks, next);
            break;
        case ERR:
            print_output(tasks, arg, STDERR);
            break;
        case OUT:
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

    for (int i = 0; i < next - 1; i++)
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
