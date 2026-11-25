#ifndef NETPULSE_REGISTRY_CLIENT_H
#define NETPULSE_REGISTRY_CLIENT_H

#include "netpulse.h"

#include <stddef.h>

int registry_fetch_devices(const char *base_url, int timeout_ms,
                           np_device **out, size_t *count,
                           char *err, size_t err_len);

int registry_post_metric(const char *base_url, const np_metric *metric,
                         int timeout_ms);

#endif
