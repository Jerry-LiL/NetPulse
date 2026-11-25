#include "registry_client.h"
#include "devices.h"
#include "http_client.h"
#include "json_util.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int origin_of(const char *base_url, char *host, size_t host_len, int *port,
                     char *err, size_t err_len) {
    if (http_parse_origin(base_url, host, host_len, port) != 0) {
        snprintf(err, err_len, "registry url must look like http://host:port");
        return -1;
    }
    return 0;
}

int registry_fetch_devices(const char *base_url, int timeout_ms,
                           np_device **out, size_t *count,
                           char *err, size_t err_len) {
    char host[NP_HOST_LEN];
    int port = 0;
    int status = 0;
    char *body = NULL;
    size_t body_len = 0;
    int rc;

    *out = NULL;
    *count = 0;
    if (origin_of(base_url, host, sizeof host, &port, err, err_len) != 0) {
        return -1;
    }
    rc = http_request(host, port, "GET", "/api/devices?enabled=1", NULL, NULL,
                      timeout_ms, &status, &body, &body_len, err, err_len);
    if (rc != 0) {
        return -1;
    }
    if (status != 200) {
        snprintf(err, err_len, "registry returned HTTP %d", status);
        free(body);
        return -1;
    }
    rc = devices_from_json(body, out, count, err, err_len);
    free(body);
    return rc;
}

int registry_post_metric(const char *base_url, const np_metric *metric, int timeout_ms) {
    char host[NP_HOST_LEN];
    char err[256];
    char payload[4096];
    int port = 0;
    int status = 0;
    char *body = NULL;
    size_t body_len = 0;
    int rc;

    if (origin_of(base_url, host, sizeof host, &port, err, sizeof err) != 0) {
        log_msg(NP_LOG_WARN, "registry", "%s", err);
        return -1;
    }
    if (metric_to_json(metric, payload, sizeof payload) != 0) {
        log_msg(NP_LOG_WARN, "registry", "could not encode metric");
        return -1;
    }
    rc = http_request(host, port, "POST", "/api/metrics", "application/json", payload,
                      timeout_ms, &status, &body, &body_len, err, sizeof err);
    if (rc != 0) {
        log_msg(NP_LOG_WARN, "registry", "metric post failed: %s", err);
        return -1;
    }
    free(body);
    if (status != 201 && status != 200) {
        log_msg(NP_LOG_WARN, "registry", "metric post returned HTTP %d", status);
        return -1;
    }
    return 0;
}
