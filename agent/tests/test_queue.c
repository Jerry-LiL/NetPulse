#include "harness.h"
#include "queue.h"

#include <pthread.h>
#include <string.h>
#include <time.h>

static void *blocking_push(void *arg) {
    np_job job;
    memset(&job, 0, sizeof job);
    job.device.id = 9;
    snprintf(job.check, sizeof job.check, "icmp");
    return (void *)(long)job_queue_push(arg, &job);
}

static int wait_for_pusher(job_queue *queue) {
    int spins = 0;
    while (job_queue_push_waiters(queue) == 0 && spins < 2000) {
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 1000000L;
        nanosleep(&ts, NULL);
        spins++;
    }
    return job_queue_push_waiters(queue) > 0;
}

int test_queue(void) {
    job_queue *queue = job_queue_create(2);
    np_job first;
    np_job second;
    np_job extra;
    np_job out;
    pthread_t thread;
    memset(&first, 0, sizeof first);
    memset(&second, 0, sizeof second);
    memset(&extra, 0, sizeof extra);
    first.device.id = 1;
    second.device.id = 2;
    extra.device.id = 3;
    snprintf(first.check, sizeof first.check, "tcp");
    snprintf(second.check, sizeof second.check, "snmp");
    snprintf(extra.check, sizeof extra.check, "icmp");

    EXPECT(queue != NULL);
    EXPECT(job_queue_push(queue, &first) == 0);
    EXPECT(job_queue_push(queue, &second) == 0);
    EXPECT(job_queue_depth(queue) == 2);
    EXPECT(pthread_create(&thread, NULL, blocking_push, queue) == 0);
    EXPECT(wait_for_pusher(queue) == 1);
    EXPECT(job_queue_pop(queue, &out) == 0);
    EXPECT(out.device.id == 1);
    EXPECT(strcmp(out.check, "tcp") == 0);
    {
        void *ret = NULL;
        EXPECT(pthread_join(thread, &ret) == 0);
        EXPECT((long)ret == 0);
    }
    EXPECT(job_queue_pop(queue, &out) == 0);
    EXPECT(out.device.id == 2);
    EXPECT(job_queue_pop(queue, &out) == 0);
    EXPECT(out.device.id == 9);
    EXPECT(job_queue_depth(queue) == 0);

    job_queue_close(queue);
    EXPECT(job_queue_push(queue, &extra) != 0);
    EXPECT(job_queue_pop(queue, &out) != 0);
    job_queue_destroy(queue);
    return 0;
}
