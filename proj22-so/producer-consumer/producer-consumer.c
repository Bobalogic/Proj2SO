#include "producer-consumer.h"
// New
#include "betterassert.h"
#include "operations.h"
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

int pcq_create(pc_queue_t *queue, size_t capacity) {
    if (capacity == 0) {
        return -1;
    }
    queue->pcq_capacity = capacity;
    queue->pcq_current_size = 0;
    queue->pcq_head = 0;
    queue->pcq_tail = 0;
    queue->pcq_buffer = malloc(capacity * sizeof(void *));
    if (queue->pcq_buffer == NULL) {
        return -1;
    }

    if (pthread_mutex_init(&queue->pcq_current_size_lock, NULL) == -1) {
        free(queue->pcq_buffer);
        return -1;
    }
    if (pthread_mutex_init(&queue->pcq_head_lock, NULL) == -1) {
        free(queue->pcq_buffer);
        pthread_mutex_destroy(&queue->pcq_current_size_lock);
        return -1;
    }
    if (pthread_mutex_init(&queue->pcq_tail_lock, NULL) == -1) {
        free(queue->pcq_buffer);
        pthread_mutex_destroy(&queue->pcq_current_size_lock);
        pthread_mutex_destroy(&queue->pcq_head_lock);
        return -1;
    }
    if (pthread_mutex_init(&queue->pcq_pusher_condvar_lock, NULL) == -1) {
        free(queue->pcq_buffer);
        pthread_mutex_destroy(&queue->pcq_current_size_lock);
        pthread_mutex_destroy(&queue->pcq_head_lock);
        pthread_mutex_destroy(&queue->pcq_tail_lock);
        return -1;
    }
    if (pthread_mutex_init(&queue->pcq_popper_condvar_lock, NULL) == -1) {
        free(queue->pcq_buffer);
        pthread_mutex_destroy(&queue->pcq_current_size_lock);
        pthread_mutex_destroy(&queue->pcq_head_lock);
        pthread_mutex_destroy(&queue->pcq_tail_lock);
        pthread_mutex_destroy(&queue->pcq_pusher_condvar_lock);
        pthread_cond_destroy(&queue->pcq_pusher_condvar);
        return -1;
    }
    if (pthread_cond_init(&queue->pcq_pusher_condvar, NULL) == -1) {
        free(queue->pcq_buffer);
        pthread_mutex_destroy(&queue->pcq_current_size_lock);
        pthread_mutex_destroy(&queue->pcq_head_lock);
        pthread_mutex_destroy(&queue->pcq_tail_lock);
        pthread_mutex_destroy(&queue->pcq_pusher_condvar_lock);
        return -1;
    }
    if (pthread_cond_init(&queue->pcq_popper_condvar, NULL) == -1) {
        free(queue->pcq_buffer);
        pthread_mutex_destroy(&queue->pcq_current_size_lock);
        pthread_mutex_destroy(&queue->pcq_head_lock);
        pthread_mutex_destroy(&queue->pcq_tail_lock);
        pthread_mutex_destroy(&queue->pcq_pusher_condvar_lock);
        pthread_cond_destroy(&queue->pcq_pusher_condvar);
        pthread_mutex_destroy(&queue->pcq_popper_condvar_lock);
        return -1;
    }
    return 0;
}

int pcq_destroy(pc_queue_t *queue) {
    pthread_mutex_destroy(&queue->pcq_current_size_lock);
    pthread_mutex_destroy(&queue->pcq_head_lock);
    pthread_mutex_destroy(&queue->pcq_tail_lock);
    pthread_mutex_destroy(&queue->pcq_pusher_condvar_lock);
    pthread_mutex_destroy(&queue->pcq_popper_condvar_lock);
    pthread_cond_destroy(&queue->pcq_pusher_condvar);
    pthread_cond_destroy(&queue->pcq_popper_condvar);
    free(queue->pcq_buffer); // only frees the memory allocated for the buffer,
                             // it does not deallocate the memory that the
                             // pointer is pointing to
    return 0;
}

int pcq_enqueue(pc_queue_t *queue, void *elem) {
    if (pthread_mutex_lock(&queue->pcq_pusher_condvar_lock) == -1) {
        WARN("failed to lock mutex: %s", strerror(errno));
        return -1;
    }
    while (queue->pcq_current_size == queue->pcq_capacity) {
        pthread_cond_wait(&queue->pcq_pusher_condvar,
                          &queue->pcq_pusher_condvar_lock);
    }
    if (pthread_mutex_unlock(&queue->pcq_pusher_condvar_lock) == -1) {
        WARN("failed to unlock mutex: %s", strerror(errno));
        return -1;
    }
    if (pthread_mutex_lock(&queue->pcq_head_lock) == -1) {
        WARN("failed to lock mutex: %s", strerror(errno));
        return -1;
    }
    queue->pcq_buffer[queue->pcq_head] = elem;
    queue->pcq_head = (queue->pcq_head + 1) % queue->pcq_capacity;
    if (pthread_mutex_unlock(&queue->pcq_head_lock) == -1) {
        WARN("failed to unlock mutex: %s", strerror(errno));
        return -1;
    }
    if (pthread_mutex_lock(&queue->pcq_current_size_lock) == -1) {
        WARN("failed to lock mutex: %s", strerror(errno));
        return -1;
    }
    queue->pcq_current_size++;
    if (pthread_mutex_unlock(&queue->pcq_current_size_lock) == -1) {
        WARN("failed to unlock mutex: %s", strerror(errno));
        return -1;
    }
    if (pthread_cond_signal(&queue->pcq_popper_condvar) == -1) {
        WARN("failed to signal condvar: %s", strerror(errno));
        return -1;
    }
    return 0;
}

void *pcq_dequeue(pc_queue_t *queue) {
    void *elem;

    if (pthread_mutex_lock(&queue->pcq_popper_condvar_lock) == -1) {
        WARN("failed to lock mutex: %s", strerror(errno));
        return NULL;
    }
    while (queue->pcq_current_size == 0) {
        pthread_cond_wait(&queue->pcq_popper_condvar,
                          &queue->pcq_popper_condvar_lock);
    }
    if (pthread_mutex_unlock(&queue->pcq_popper_condvar_lock) == -1) {
        WARN("failed to unlock mutex: %s", strerror(errno));
        return NULL;
    }
    if (pthread_mutex_lock(&queue->pcq_tail_lock) == -1) {
        WARN("failed to lock mutex: %s", strerror(errno));
        return NULL;
    }
    elem = queue->pcq_buffer[queue->pcq_tail];
    queue->pcq_tail = (queue->pcq_tail + 1) % queue->pcq_capacity;
    if (pthread_mutex_unlock(&queue->pcq_tail_lock) == -1) {
        WARN("failed to unlock mutex: %s", strerror(errno));
        return NULL;
    }
    if (pthread_mutex_lock(&queue->pcq_current_size_lock) == -1) {
        WARN("failed to lock mutex: %s", strerror(errno));
        return NULL;
    }
    queue->pcq_current_size--;
    if (pthread_mutex_unlock(&queue->pcq_current_size_lock) == -1) {
        WARN("failed to unlock mutex: %s", strerror(errno));
        return NULL;
    }

    pthread_cond_signal(&queue->pcq_pusher_condvar);

    return elem;
}