#include "logging.h"
// New
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <operations.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
    int fd;
    char name[32];
    bool isFree;
    int size;
    int n_pub;
    int n_sub;
} Box;

Box box_list[16];
int box_list_size;

pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t subCond;

/** This send_msg function, unlike the others, returns an int,
 * since it needs to return -1 in case the destination pipe is
 * closed
 */
int send_msg(int dest, char const *str, size_t size) {

    ssize_t ret = write(dest, str, size);

    if (ret < 0) {
        printf("[INFO]: Destination pipe closed\n");
        exit(EXIT_FAILURE);
        return -1;
    }
    return 0;
}

bool isLastBox(int i) { return i + 1 == box_list_size; }

int findBox(char name[32]) {
    int j = -1;
    for (int i = 0; i < box_list_size; i++) {
        if (strcmp(box_list[i].name, name) == 0)
            return i;
    }
    return j; // Returns -1 if box doesn't exist
}

void createBox(char box_name[32]) {
    Box box;

    char boxFile[33] = "/";
    strcat(boxFile, box_name);

    box.fd = tfs_open(boxFile, TFS_O_CREAT);

    // Box initializations
    strcpy(box.name, box_name);
    box.size = 0;
    box.n_pub = 0;
    box.n_sub = 0;
    box.isFree = true;
    box_list[box_list_size] = box;
    box_list_size++;
}

int removeBox(char box_name[32]) {
    int i = findBox(box_name);
    if (i == -1)
        return -1;
    box_list[i].isFree = true;
    char boxFile[33] = "/";
    strcat(boxFile, box_name);
    return (tfs_unlink(boxFile));
}

void delFromBList(char box_name[32]) {
    int i = findBox(box_name);
    for (int j = i; j < box_list_size - 1; j++) {
        box_list[j] = box_list[j + 1];
    }
    box_list_size--;
}

void listBox(Box box, int i, int ses_pipe) {
    char manListReponse[MAN_LIST_SIZE];
    u_int8_t last, code;
    u_int64_t size, n_pub, n_sub;

    if (isLastBox(i))
        last = 1;
    else
        last = 0;

    code = 8;
    size = (u_int64_t)box.size;
    n_pub = (u_int64_t)box.n_pub;
    n_sub = (u_int64_t)box.n_sub;

    // Copying information to buffer
    memcpy(manListReponse, &code, UINT8_SIZE);
    memcpy(manListReponse + UINT8_SIZE, &last, UINT8_SIZE);
    memcpy(manListReponse + 2 * UINT8_SIZE, box.name, 32);
    memcpy(manListReponse + 2 * UINT8_SIZE + 32, &size, UINT64_SIZE);
    memcpy(manListReponse + 2 * UINT8_SIZE + 32 + UINT64_SIZE, &n_pub,
           UINT64_SIZE);
    memcpy(manListReponse + 2 * UINT8_SIZE + 32 + 2 * UINT64_SIZE, &n_sub,
           UINT64_SIZE);

    send_msg(ses_pipe, manListReponse, MAN_LIST_SIZE);
}

void listAllBoxes(int ses_pipe) {
    for (int i = 0; i < box_list_size; i++) {
        listBox(box_list[i], i, ses_pipe);
    }
}

bool canCreateBox(char box_name[32]) {

    if (findBox(box_name) != -1)
        return false;

    return true;
}

bool canSendMsgToBox(const char *msg, char box_name[32]) {
    if (findBox(box_name) == -1) {
        return false;
    }
    if (strlen(msg) > MSG_SIZE + OP_CODE_SIZE) {
        return false;
    }
    return true;
}

bool canSubLogIn(int max_ses, int curr_ses, int ses_pipe, char box_name[32]) {
    if (curr_ses + 1 > max_ses) {
        close(ses_pipe);
        return false; // New session would exceed max_sessions
    }

    char boxFile[33] = "/";
    strcat(boxFile, box_name);

    if (tfs_open(boxFile, 0) == -1) {
        close(ses_pipe);
        return false; // If box doesn't exist
    }
    return true;
}

bool canPubLogIn(int max_ses, int curr_ses, int ses_pipe, char box_name[32]) {
    if (curr_ses + 1 > max_ses) {
        close(ses_pipe);
        return false; // New session would exceed max_sessions
    }

    int j = 0;
    for (int i = 0; i < box_list_size; i++) {
        if (strcmp(box_list[i].name, box_name) == 0)
            j = 1;
    }
    if (j == 0) // Box does not exist
        return false;

    int i = findBox(box_name);
    if (!box_list[i].isFree) {
        return false; // Box already has a publisher
    }

    return true;
}

void tryCreateBox(int ses_pipe, char box_name[32]) {

    char msg[MAN_RESPONSE_SIZE];
    memset(msg, 0, MAN_RESPONSE_SIZE);

    if (!canCreateBox(box_name)) { // Couldn't create box
        strcpy(msg, "40");
        strcpy(msg + 2, "Couldn't create box");
        if (send_msg(ses_pipe, msg, MAN_RESPONSE_SIZE) == -1)
            return;
    } else {
        createBox(box_name);
        strcpy(msg, "41");
        if (send_msg(ses_pipe, msg, MAN_RESPONSE_SIZE) == -1)
            return;
    }
}

void tryRemoveBox(int ses_pipe, char box_name[32]) {
    char msg[MAN_RESPONSE_SIZE];
    memset(msg, 0, MAN_RESPONSE_SIZE);

    if (removeBox(box_name) == -1) { // Box did not exist
        strcpy(msg, "60");
        strcpy(msg + 2, "Couldn't remove box");
        if (send_msg(ses_pipe, msg, MAN_RESPONSE_SIZE) == -1)
            return;
    } else {
        delFromBList(box_name);
        strcpy(msg, "61");
        if (send_msg(ses_pipe, msg, MAN_RESPONSE_SIZE) == -1)
            return;
    }
}

void sendBoxContents(int ses_pipe, char box_name[32]) {

    int i = findBox(box_name);

    size_t file_size = (size_t)box_list[i].size;
    char box_buffer[file_size];

    char boxFile[33] = "/";
    strcat(boxFile, box_name);

    int fd = tfs_open(boxFile, 0);

    // Reading everything inside the file up until now
    ssize_t bytes_read = tfs_read(fd, box_buffer, file_size);

    if (bytes_read < 0) // Read negative bytes
        return;
    if (send_msg(ses_pipe, box_buffer, (size_t)bytes_read) == -1)
        return;

    tfs_close(fd);

    while (true) { // Infinitely waiting for new messages in box
        char box_buffer2[MSG_SIZE];
        memset(box_buffer2, 0, MSG_SIZE);

        pthread_cond_wait(&subCond,
                          &g_mutex); // While nothing new is written in box

        fd = tfs_open(boxFile, 0);
        bytes_read = tfs_read(fd, box_buffer2, MSG_SIZE - 1);
        if (bytes_read < 0) // Read negative bytes
            break;

        if (send_msg(ses_pipe, box_buffer2, (size_t)bytes_read) == -1)
            break;
        memset(box_buffer2, 0, MSG_SIZE);
    }
    tfs_close(fd);
}

void trySendMsgToBox(char msg[MSG_SIZE], char box_name[32]) {
    if (!canSendMsgToBox(msg, box_name)) {
        printf("[ERROR]: Couldn't send message to box\n");
        return;
    } else {
        char boxFile[33] = "/";
        strcat(boxFile, box_name);

        int box_fd = tfs_open(boxFile, TFS_O_APPEND);
        int i = findBox(box_name);

        ssize_t bytes_written = tfs_write(box_fd, msg, strlen(msg) + 1);
        if (bytes_written == -1) {
            return;
        } else
            box_list[i].size += (int)strlen(msg) + 1;
        pthread_cond_signal(&subCond);
        tfs_close(box_fd);
    }
}

void waitPubMsg(int ses_pipe, char box_name[32]) {
    while (true) {
        char buffer[MSG_SIZE];
        memset(buffer, 0, MSG_SIZE);
        ssize_t ret = read(ses_pipe, buffer, MSG_SIZE - 1);
        if (ret == 0) {
            // ret == 0 indicates EOF
            fprintf(stderr, "[INFO]: pipe closed\n");
            return;
        } else if (ret == -1) {
            // ret == -1 indicates error
            fprintf(stderr, "[ERR]: read failed: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        if (strcmp(buffer, "") == 0) {
        } else
            trySendMsgToBox(buffer, box_name);
    }
}

int main(int argc, char **argv) {
    int curr_ses = 0, ses_pipe, max_ses;
    box_list_size = 0;
    max_ses = 10;
    char ses_pipe_name[SES_PIPE_SIZE];
    char box_name[32];

    tfs_init(NULL);
    pthread_cond_init(&subCond, NULL);

    if (argc != 3)
        return 0;

    // Remove pipe if it already exists
    if (unlink(argv[1]) != 0 && errno != ENOENT) {
        fprintf(stderr, "[ERR]: unlink(%s) failed: %s\n", argv[1],
                strerror(errno));
        exit(EXIT_FAILURE);
    }

    // Creating the server named pipe
    if (mkfifo(argv[1], 0640) != 0) {
        fprintf(stderr, "[ERR]: mkfifo failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    int ser_pipe = open(argv[1], O_RDONLY);

    // Infinitely waiting for a message (unless errors ocurr)
    while (true) {
        char buffer[BUFFER_SIZE];
        memset(buffer, 0, BUFFER_SIZE);
        ssize_t ret = read(ser_pipe, buffer, BUFFER_SIZE);

        if (ret == 0) { // EOF
            close(ser_pipe);
            ser_pipe = open(argv[1], O_RDONLY);
        } else if (ret == -1) { // Error
            fprintf(stderr, "[ERROR]: read failed: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }

        if (strcmp(buffer, "") == 0) {
        }

        memcpy(ses_pipe_name, buffer + 1, SES_PIPE_SIZE - 1); //dest, src, nº of bytes to be copied
        memcpy(box_name, buffer + 1 + 256, BOX_NAME_SIZE - 1);

        switch (buffer[0]) {
        case '1': // Message is request from publisher to log in
            ses_pipe =
                open(ses_pipe_name, O_RDONLY); // Opening session pipe as reader
            if (canPubLogIn(max_ses, curr_ses, ses_pipe, box_name)) {
                int i = findBox(box_name);

                box_list[i].isFree =
                    false; // No more publishers can associate to this box
                box_list[i].n_pub++;
                curr_ses++;
                waitPubMsg(ses_pipe, box_name);
            } else {
                close(ses_pipe); // Closing client session pipe
            }
            break;

        case '2': // Message is request from subscriber to log in
            ses_pipe =
                open(ses_pipe_name, O_WRONLY); // Opening session pipe as writer
            if (canSubLogIn(max_ses, curr_ses, ses_pipe, box_name)) {
                int i = findBox(box_name);

                box_list[i].n_sub++;
                curr_ses++;
                sendBoxContents(ses_pipe, box_name);
            } else {
                curr_ses--;
                close(ses_pipe); // Closing client session pipe
            }
            break;

        case '3': // Message is a request to create a box
            ses_pipe =
                open(ses_pipe_name, O_WRONLY); // Opening session pipe as writer
            tryCreateBox(ses_pipe, box_name);
            close(ses_pipe);
            break;

        case '5': // Message is a request to remove a box
            ses_pipe =
                open(ses_pipe_name, O_WRONLY); // Opening session pipe as writer
            tryRemoveBox(ses_pipe, box_name);
            close(ses_pipe);
            break;

        case '7': // Message is a request to list all boxes
            ses_pipe =
                open(ses_pipe_name, O_WRONLY); // Opnineg session pipe as writer
            listAllBoxes(ses_pipe);
            close(ses_pipe);
            break;

        default:
            break;
        }
    }
    return 0;
}