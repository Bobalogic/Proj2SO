#ifndef __UTILS_LOGGING_H__
#define __UTILS_LOGGING_H__

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUFFER_SIZE 289
#define MSG_SIZE 1024
#define SES_PIPE_SIZE 256
#define OP_CODE_SIZE 1
#define BOX_NAME_SIZE 32
#define MAN_RESPONSE_SIZE 1026
#define MAN_LIST_SIZE 2 * sizeof(u_int8_t) + 32 + 3 * sizeof(u_int64_t)
#define UINT8_SIZE sizeof(u_int8_t)
#define UINT64_SIZE sizeof(u_int64_t)

typedef enum {
    LOG_QUIET = 0,
    LOG_NORMAL = 1,
    LOG_VERBOSE = 2,
} log_level_t;

typedef enum {
    REQUEST,
    MESSAGE,
} msg_type;

void set_log_level(log_level_t level);
extern log_level_t g_level;

#define INFO(...)                                                              \
    do {                                                                       \
        char buf[2048];                                                        \
        snprintf(buf, 2048, __VA_ARGS__);                                      \
        fprintf(stderr, "[INFO]:  %s:%d :: %s :: %s\n", __FILE__, __LINE__,    \
                __func__, buf);                                                \
    } while (0);

#define PANIC(...)                                                             \
    do {                                                                       \
        char buf[2048];                                                        \
        snprintf(buf, 2048, __VA_ARGS__);                                      \
        fprintf(stderr, "[PANIC]: %s:%d :: %s :: %s\n", __FILE__, __LINE__,    \
                __func__, buf);                                                \
        exit(EXIT_FAILURE);                                                    \
    } while (0);

#define WARN(...)                                                              \
    do {                                                                       \
        if (g_level == LOG_NORMAL || g_level == LOG_VERBOSE) {                 \
            char buf[2048];                                                    \
            snprintf(buf, 2048, __VA_ARGS__);                                  \
            fprintf(stderr, "[WARN]:  %s:%d :: %s :: %s\n", __FILE__,          \
                    __LINE__, __func__, buf);                                  \
        }                                                                      \
    } while (0);

#define LOG(...)                                                               \
    do {                                                                       \
        if (g_level == LOG_NORMAL || g_level == LOG_VERBOSE) {                 \
            char buf[2048];                                                    \
            snprintf(buf, 2048, __VA_ARGS__);                                  \
            fprintf(stderr, "[LOG]:   %s:%d :: %s :: %s\n", __FILE__,          \
                    __LINE__, __func__, buf);                                  \
        }                                                                      \
    } while (0);

#define DEBUG(...)                                                             \
    do {                                                                       \
        if (g_level == LOG_VERBOSE) {                                          \
            char buf[2048];                                                    \
            snprintf(buf, 2048, __VA_ARGS__);                                  \
            fprintf(stderr, "[DEBUG]: %s:%d :: %s :: %s\n", __FILE__,          \
                    __LINE__, __func__, buf);                                  \
        }                                                                      \
    } while (0);

#endif // __UTILS_LOGGING_H__
