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

void send_msg(int dest, char const *str, size_t size) {

    ssize_t ret = write(dest, str, size);

    if (ret < 0) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
}

void sendManReqList(char ses_pipe_name[256], int ser_pipe) {
    char msg[BUFFER_SIZE];
    memset(msg, 0, BUFFER_SIZE);
    msg[0] = '7';

    strncpy(msg + 1, ses_pipe_name, SES_PIPE_SIZE);

    send_msg(ser_pipe, msg, BUFFER_SIZE - BOX_NAME_SIZE);
}

void sendManReqNormal(char op_code, char ses_pipe_name[256], char box_name[32],
                      int ser_pipe) {

    char msg[BUFFER_SIZE];
    memset(msg, 0, BUFFER_SIZE);
    msg[0] = op_code;

    strncpy(msg + 1, ses_pipe_name, 256 - 1);
    strncpy(msg + 1 + 256, box_name, 32 - 1);

    send_msg(ser_pipe, msg, BUFFER_SIZE);
}

void waitListResponse(int ses_pipe) {
    while (true) {
        // Waiting for repsonse message from mbroker and printing it
        char buffer[MAN_LIST_SIZE];
        memset(buffer, 0, MAN_LIST_SIZE);
        char box_name[BOX_NAME_SIZE];
        u_int8_t code, last;
        u_int64_t size, n_pub, n_sub;

        ssize_t ret = read(ses_pipe, buffer, MAN_LIST_SIZE - 1);

        if (ret < 0)
            fprintf(stderr, "[ERR]: Negative bytes: %s\n", strerror(errno));

        // Copying information from buffer to variables
        memcpy(&code, buffer, UINT8_SIZE);
        memcpy(&last, buffer + UINT8_SIZE, UINT8_SIZE);
        memcpy(box_name, buffer + 2 * UINT8_SIZE, 32);
        memcpy(&size, buffer + 2 * UINT8_SIZE + 32, UINT64_SIZE);
        memcpy(&n_pub, buffer + 2 * UINT8_SIZE + 32 + UINT64_SIZE, UINT64_SIZE);
        memcpy(&n_sub, buffer + 2 * UINT8_SIZE + 32 + 2 * UINT64_SIZE,
               UINT64_SIZE);

        fprintf(stdout, "%s %zu %zu %zu\n", box_name, size, n_pub, n_sub);
        if (last == 1)
            break;
    }
}

void waitNormalResponse(int ses_pipe) {
    // Waiting for response message from mbroker and printing it
    char buffer[MAN_RESPONSE_SIZE];
    ssize_t ret = read(ses_pipe, buffer, MAN_RESPONSE_SIZE - 1);
    if (ret < 0)
        fprintf(stderr, "[ERR]: Negative bytes: %s\n", strerror(errno));

    if (buffer[1] == '1')
        fprintf(stdout, "OK\n");
    else
        fprintf(stdout, "ERROR: %s\n", buffer + 2);
}

int main(int argc, char **argv) {

    if (argc != 4 && argc != 5)
        return 0;

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
    if (strcmp(argv[3], "list") != 0) {
        if (strcmp(argv[3], "create") == 0)
            sendManReqNormal('3', argv[2], argv[4], ser_pipe);
        else
            sendManReqNormal('5', argv[2], argv[4], ser_pipe);
    }

    else
        sendManReqList(argv[2], ser_pipe);

    // Opening the session pipe as a reader
    int ses_pipe = open(argv[2], O_RDONLY);
    if (ses_pipe == -1) {
        fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (strcmp(argv[3], "list") == 0)
        waitListResponse(ses_pipe);

    else
        waitNormalResponse(ses_pipe);

    // Ending session
    close(ses_pipe);

    return -1;
}
