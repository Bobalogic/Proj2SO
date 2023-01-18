#include "logging.h"
// New
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int msg_read;

void send_msg(int dest, char const *str, size_t size) {

    ssize_t ret = write(dest, str, size);

    if (ret < 0) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
}

void sig_handler(int sig) {
    if (sig == SIGINT) {
        printf("%d\n", msg_read); // Printing amount of messages read
        exit(0);
    }
}

void sendSubReq(char ses_pipe_name[256], char box_name[32], int ser_pipe) {
    char msg[BUFFER_SIZE];
    memset(msg, 0, BUFFER_SIZE);
    msg[0] = '2';

    strncpy(msg + 1, ses_pipe_name, 256 - 1);
    strncpy(msg + 1 + 256, box_name, 32 - 1);

    send_msg(ser_pipe, msg, BUFFER_SIZE);
}

int main(int argc, char **argv) {

    if (argc != 4)
        return 0;

    msg_read = 0;

    // Opening server pipe as writer
    int ser_pipe = open(argv[1], O_WRONLY);
    if (ser_pipe == -1) {
        fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // Remove pipe if it already exists
    if (unlink(argv[2]) != 0 && errno != ENOENT) {
        fprintf(stderr, "[ERR]: unlink(%s) failed: %s\n", argv[2],
                strerror(errno));
        exit(EXIT_FAILURE);
    }

    // Creating the session named pipe
    if (mkfifo(argv[2], 0640) != 0) {
        fprintf(stderr, "[ERR]: mkfifo failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // Sending request message to server
    sendSubReq(argv[2], argv[3], ser_pipe);

    // Opening session pipe as a reader
    int ses_pipe = open(argv[2], O_RDONLY);
    if (ses_pipe == -1) {
        fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    while (true) {
        signal(SIGINT, sig_handler); // To deal with SIGINT

        char buffer[1];
        ssize_t ret = read(ses_pipe, buffer, 1);
        if (ret == 0) {
            // ret == 0 indicates EOF
            fprintf(stderr, "[INFO]: pipe closed\n");
            return 0;
        } else if (ret == -1) {
            // ret == -1 indicates error
            fprintf(stderr, "[ERR]: read failed: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        if (strcmp(buffer, "") == 0) {
            printf("\n");
            msg_read++;
        }

        else {
            fputs(buffer, stdout);
        }
    }

    return -1;
}
