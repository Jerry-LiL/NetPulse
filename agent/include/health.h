#ifndef NETPULSE_HEALTH_H
#define NETPULSE_HEALTH_H

#include <stddef.h>

typedef struct {
    unsigned long checks_total;
    unsigned long checks_failed;
    size_t queue_depth;
    int registry_state; /* -1 not configured, 0 last fetch failed, 1 ok */
    int ready;
} health_snapshot;

typedef void (*health_snapshot_fn)(void *userdata, health_snapshot *out);

typedef struct health_server health_server;

health_server *health_start(const char *addr, int port, int *bound_port,
                            health_snapshot_fn fn, void *userdata);
void health_stop(health_server *server);

#endif
