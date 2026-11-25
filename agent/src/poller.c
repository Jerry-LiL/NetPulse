#include "poller.h"
#include "checks.h"
#include "devices.h"
#include "json_util.h"
#include "log.h"
#include "queue.h"
#include "registry_client.h"
#include "signals.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct poller {
    np_config cfg;
    job_queue *queue;
    pthread_mutex_t mu;
    pthread_cond_t idle;
    pthread_mutex_t emit_mu;
    int outstanding;
    unsigned long checks_total;
    unsigned long checks_failed;
    int registry_state;
    int ready;
};

static void sleep_ms(int ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) < 0 && errno == EINTR) {
        if (signals_stop_requested()) {
            return;
        }
    }
}

static void execute(poller *p, const np_job *job) {
    np_metric metric;
    char line[4096];
    metric_prepare(&metric, &job->device, job->check);
    if (strcmp(job->check, "icmp") == 0) {
        icmp_probe(job->device.host, p->cfg.check_timeout_ms, &metric);
    } else if (strcmp(job->check, "tcp") == 0) {
        tcp_probe(job->device.host, job->device.tcp_port, p->cfg.check_timeout_ms, &metric);
    } else if (strcmp(job->check, "snmp") == 0) {
        snmp_probe(job->device.host, job->device.snmp_port, job->device.snmp_community,
                   job->device.snmp_oid, p->cfg.check_timeout_ms, &metric);
    } else {
        snprintf(metric.detail, sizeof metric.detail, "unknown check");
    }

    if (metric_to_json(&metric, line, sizeof line) != 0) {
        log_msg(NP_LOG_ERROR, "poller", "could not encode metric for %s", metric.device_name);
    } else {
        pthread_mutex_lock(&p->emit_mu);
        fprintf(stdout, "%s\n", line);
        fflush(stdout);
        pthread_mutex_unlock(&p->emit_mu);
        log_msg(NP_LOG_DEBUG, "poller", "%s %s %s", metric.device_name, metric.check,
                metric.ok ? "ok" : "failed");
    }
    if (p->cfg.registry_url[0] != '\0') {
        if (registry_post_metric(p->cfg.registry_url, &metric, p->cfg.check_timeout_ms) != 0) {
            log_msg(NP_LOG_WARN, "registry", "dropped %s result for %s", metric.check,
                    metric.device_name);
        }
    }
    pthread_mutex_lock(&p->mu);
    p->checks_total++;
    if (!metric.ok) {
        p->checks_failed++;
    }
    pthread_mutex_unlock(&p->mu);
}

static void *worker_main(void *arg) {
    poller *p = arg;
    np_job job;
    while (job_queue_pop(p->queue, &job) == 0) {
        execute(p, &job);
        pthread_mutex_lock(&p->mu);
        p->outstanding--;
        if (p->outstanding == 0) {
            pthread_cond_broadcast(&p->idle);
        }
        pthread_mutex_unlock(&p->mu);
    }
    return NULL;
}

static int enqueue_check(poller *p, const np_device *device, const char *check) {
    np_job job;
    memset(&job, 0, sizeof job);
    job.device = *device;
    snprintf(job.check, sizeof job.check, "%s", check);
    pthread_mutex_lock(&p->mu);
    p->outstanding++;
    pthread_mutex_unlock(&p->mu);
    if (job_queue_push(p->queue, &job) != 0) {
        pthread_mutex_lock(&p->mu);
        p->outstanding--;
        if (p->outstanding == 0) {
            pthread_cond_broadcast(&p->idle);
        }
        pthread_mutex_unlock(&p->mu);
        return -1;
    }
    return 0;
}

static int enqueue_device(poller *p, const np_device *device, int *queued) {
    if (device->icmp_enabled) {
        if (enqueue_check(p, device, "icmp") != 0) {
            return -1;
        }
        (*queued)++;
    }
    if (device->tcp_port > 0) {
        if (enqueue_check(p, device, "tcp") != 0) {
            return -1;
        }
        (*queued)++;
    }
    if (device->snmp_community[0] != '\0' && device->snmp_oid[0] != '\0') {
        if (enqueue_check(p, device, "snmp") != 0) {
            return -1;
        }
        (*queued)++;
    }
    return 0;
}

static int load_devices(poller *p, np_device **devices, size_t *count) {
    char err[256];
    int rc = -1;
    err[0] = '\0';
    *devices = NULL;
    *count = 0;
    if (p->cfg.registry_url[0] != '\0') {
        rc = registry_fetch_devices(p->cfg.registry_url, p->cfg.check_timeout_ms,
                                    devices, count, err, sizeof err);
        pthread_mutex_lock(&p->mu);
        p->registry_state = rc == 0 ? 1 : 0;
        if (rc == 0) {
            p->ready = 1;
        }
        pthread_mutex_unlock(&p->mu);
        if (rc == 0) {
            return 0;
        }
        log_msg(NP_LOG_WARN, "registry", "device fetch failed: %s", err[0] ? err : "unknown error");
        if (p->cfg.devices_file[0] == '\0') {
            return -1;
        }
        log_msg(NP_LOG_INFO, "poller", "falling back to %s", p->cfg.devices_file);
    }
    rc = devices_load_file(p->cfg.devices_file, devices, count, err, sizeof err);
    pthread_mutex_lock(&p->mu);
    if (p->cfg.registry_url[0] == '\0') {
        p->registry_state = -1;
    }
    if (rc == 0) {
        p->ready = 1;
    }
    pthread_mutex_unlock(&p->mu);
    if (rc != 0) {
        log_msg(NP_LOG_ERROR, "poller", "devices file: %s", err[0] ? err : "unknown error");
        return -1;
    }
    return 0;
}

static int run_cycle(poller *p) {
    np_device *devices = NULL;
    size_t count = 0;
    size_t i;
    int queued = 0;
    if (load_devices(p, &devices, &count) != 0) {
        return -1;
    }
    for (i = 0; i < count; i++) {
        if (enqueue_device(p, &devices[i], &queued) != 0) {
            devices_free(devices);
            log_msg(NP_LOG_WARN, "poller", "stopped queueing because the agent is shutting down");
            return -1;
        }
    }
    devices_free(devices);
    log_msg(NP_LOG_INFO, "poller", "queued %d checks across %zu devices", queued, count);
    return 0;
}

static void wait_until_idle(poller *p) {
    pthread_mutex_lock(&p->mu);
    while (p->outstanding > 0 && !signals_stop_requested()) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 200000000L;
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec += 1;
            ts.tv_nsec -= 1000000000L;
        }
        pthread_cond_timedwait(&p->idle, &p->mu, &ts);
    }
    pthread_mutex_unlock(&p->mu);
}

poller *poller_new(const np_config *cfg) {
    poller *p = calloc(1, sizeof(*p));
    if (!p) {
        return NULL;
    }
    p->cfg = *cfg;
    p->queue = job_queue_create(1024);
    if (!p->queue) {
        free(p);
        return NULL;
    }
    pthread_mutex_init(&p->mu, NULL);
    pthread_cond_init(&p->idle, NULL);
    pthread_mutex_init(&p->emit_mu, NULL);
    p->registry_state = cfg->registry_url[0] ? 0 : -1;
    return p;
}

void poller_fill_snapshot(void *userdata, health_snapshot *out) {
    poller *p = userdata;
    memset(out, 0, sizeof(*out));
    pthread_mutex_lock(&p->mu);
    out->checks_total = p->checks_total;
    out->checks_failed = p->checks_failed;
    out->registry_state = p->registry_state;
    out->ready = p->ready;
    pthread_mutex_unlock(&p->mu);
    out->queue_depth = job_queue_depth(p->queue);
}

int poller_run(poller *p) {
    pthread_t *workers;
    int i;
    int started = 0;
    int rc = 0;
    if (!p || p->cfg.workers < 1) {
        return -1;
    }
    workers = calloc((size_t)p->cfg.workers, sizeof(*workers));
    if (!workers) {
        return -1;
    }
    for (i = 0; i < p->cfg.workers; i++) {
        if (pthread_create(&workers[i], NULL, worker_main, p) != 0) {
            log_msg(NP_LOG_ERROR, "poller", "could not start worker %d", i);
            rc = -1;
            break;
        }
        started++;
    }
    if (rc == 0) {
        if (p->cfg.run_once) {
            rc = run_cycle(p);
            if (rc == 0) {
                wait_until_idle(p);
            }
        } else {
            while (!signals_stop_requested()) {
                if (run_cycle(p) != 0) {
                    log_msg(NP_LOG_WARN, "poller", "cycle failed; retrying");
                }
                for (i = 0; i < p->cfg.poll_interval_s && !signals_stop_requested(); i++) {
                    sleep_ms(1000);
                }
            }
            log_msg(NP_LOG_INFO, "poller", "shutdown requested");
        }
    }
    job_queue_close(p->queue);
    for (i = 0; i < started; i++) {
        pthread_join(workers[i], NULL);
    }
    free(workers);
    return rc;
}

void poller_free(poller *p) {
    if (!p) {
        return;
    }
    job_queue_destroy(p->queue);
    pthread_cond_destroy(&p->idle);
    pthread_mutex_destroy(&p->emit_mu);
    pthread_mutex_destroy(&p->mu);
    free(p);
}
