#include "utils.h"
#include "err.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COMMAND_LENGTH 511

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

int main()
{
    bool is_eof;
    size_t buffer_size = COMMAND_LENGTH;
    char* buffer = malloc(buffer_size * sizeof(char));
    char** parts;

    if (!buffer) {
        perror("Failed to allocate input buffer");
        exit(1);
    }

    bool should_quit = false;

    while (!should_quit) {
        is_eof = !read_line(buffer, buffer_size, stdin);
        parts = split_string(buffer);
        if (is_eof) break;
        switch (get_command(parts[0])) {
            case RUN:
                break;
            case OUT:
                break;
            case ERR:
                break;
            case KILL:
                break;
            case SLEEP:
                break;
            case QUIT:
                should_quit = true;
                break;
        }
    }

    return 0;
}
