#include "harness.h"
#include "health.h"
#include "http_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_ready = 1;

static void snapshot(void *userdata, health_snapshot *out) {
    (void)userdata;
    memset(out, 0, sizeof *out);
    out->checks_total = 4;
    out->checks_failed = 1;
    out->queue_depth = 2;
    out->registry_state = 1;
    out->ready = g_ready;
}

int test_health(void) {
    health_server *server;
    int port = 0;
    int status = 0;
    char *body = NULL;
    char err[128];

    server = health_start("127.0.0.1", 0, &port, snapshot, NULL);
    EXPECT(server != NULL);
    EXPECT(port > 0);
    EXPECT(http_request("127.0.0.1", port, "GET", "/health", NULL, NULL, 1000,
                        &status, &body, NULL, err, sizeof err) == 0);
    EXPECT(status == 200);
    EXPECT(body && strstr(body, "\"checks_total\":4") != NULL);
    EXPECT(body && strstr(body, "\"checks_failed\":1") != NULL);
    EXPECT(body && strstr(body, "\"registry\":\"ok\"") != NULL);
    free(body);
    body = NULL;

    EXPECT(http_request("127.0.0.1", port, "GET", "/ready", NULL, NULL, 1000,
                        &status, &body, NULL, err, sizeof err) == 0);
    EXPECT(status == 200);
    free(body);
    body = NULL;

    g_ready = 0;
    EXPECT(http_request("127.0.0.1", port, "GET", "/ready", NULL, NULL, 1000,
                        &status, &body, NULL, err, sizeof err) == 0);
    EXPECT(status == 503);
    EXPECT(body && strstr(body, "not_ready") != NULL);
    free(body);
    body = NULL;

    EXPECT(http_request("127.0.0.1", port, "GET", "/nope", NULL, NULL, 1000,
                        &status, &body, NULL, err, sizeof err) == 0);
    EXPECT(status == 404);
    free(body);
    health_stop(server);
    return 0;
}
