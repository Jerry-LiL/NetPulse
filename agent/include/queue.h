#ifndef NETPULSE_QUEUE_H
#define NETPULSE_QUEUE_H

#include "netpulse.h"

#include <stddef.h>

typedef struct {
    np_device device;
    char check[NP_CHECK_LEN];
} np_job;

typedef struct job_queue job_queue;

job_queue *job_queue_create(size_t capacity);
void job_queue_destroy(job_queue *queue);
int job_queue_push(job_queue *queue, const np_job *job);
int job_queue_pop(job_queue *queue, np_job *job);
void job_queue_close(job_queue *queue);
size_t job_queue_depth(job_queue *queue);
size_t job_queue_push_waiters(job_queue *queue);

#endif
