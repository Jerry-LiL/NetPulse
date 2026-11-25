#ifndef NETPULSE_POLLER_H
#define NETPULSE_POLLER_H

#include "health.h"
#include "netpulse.h"

typedef struct poller poller;

poller *poller_new(const np_config *cfg);
int poller_run(poller *p);
void poller_fill_snapshot(void *userdata, health_snapshot *out);
void poller_free(poller *p);

#endif
