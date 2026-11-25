#include "queue.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

struct job_queue {
    np_job *slots;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t count;
    size_t push_waiters;
    int closed;
    pthread_mutex_t mu;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
};

job_queue *job_queue_create(size_t capacity) {
    job_queue *queue;
    if (capacity == 0) {
        return NULL;
    }
    queue = calloc(1, sizeof(*queue));
    if (!queue) {
        return NULL;
    }
    queue->slots = calloc(capacity, sizeof(*queue->slots));
    if (!queue->slots) {
        free(queue);
        return NULL;
    }
    queue->capacity = capacity;
    pthread_mutex_init(&queue->mu, NULL);
    pthread_cond_init(&queue->not_empty, NULL);
    pthread_cond_init(&queue->not_full, NULL);
    return queue;
}

void job_queue_destroy(job_queue *queue) {
    if (!queue) {
        return;
    }
    pthread_cond_destroy(&queue->not_full);
    pthread_cond_destroy(&queue->not_empty);
    pthread_mutex_destroy(&queue->mu);
    free(queue->slots);
    free(queue);
}

int job_queue_push(job_queue *queue, const np_job *job) {
    pthread_mutex_lock(&queue->mu);
    while (queue->count == queue->capacity && !queue->closed) {
        queue->push_waiters++;
        pthread_cond_wait(&queue->not_full, &queue->mu);
        queue->push_waiters--;
    }
    if (queue->closed) {
        pthread_mutex_unlock(&queue->mu);
        return -1;
    }
    queue->slots[queue->tail] = *job;
    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->count++;
    pthread_cond_signal(&queue->not_empty);
    pthread_mutex_unlock(&queue->mu);
    return 0;
}

int job_queue_pop(job_queue *queue, np_job *job) {
    pthread_mutex_lock(&queue->mu);
    while (queue->count == 0 && !queue->closed) {
        pthread_cond_wait(&queue->not_empty, &queue->mu);
    }
    if (queue->count == 0 && queue->closed) {
        pthread_mutex_unlock(&queue->mu);
        return -1;
    }
    *job = queue->slots[queue->head];
    queue->head = (queue->head + 1) % queue->capacity;
    queue->count--;
    pthread_cond_signal(&queue->not_full);
    pthread_mutex_unlock(&queue->mu);
    return 0;
}

void job_queue_close(job_queue *queue) {
    pthread_mutex_lock(&queue->mu);
    queue->closed = 1;
    pthread_cond_broadcast(&queue->not_empty);
    pthread_cond_broadcast(&queue->not_full);
    pthread_mutex_unlock(&queue->mu);
}

size_t job_queue_depth(job_queue *queue) {
    size_t depth;
    pthread_mutex_lock(&queue->mu);
    depth = queue->count;
    pthread_mutex_unlock(&queue->mu);
    return depth;
}

size_t job_queue_push_waiters(job_queue *queue) {
    size_t waiters;
    pthread_mutex_lock(&queue->mu);
    waiters = queue->push_waiters;
    pthread_mutex_unlock(&queue->mu);
    return waiters;
}
