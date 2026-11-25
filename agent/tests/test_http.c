#include "devices.h"
#include "harness.h"
#include "http_client.h"
#include "registry_client.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct {
    int fd;
    int port;
    pthread_t thread;
    atomic_int stop;
    char last_body[1024];
    int posts;
} fake_registry;

static void respond(int fd, int status, const char *body) {
    char buf[2048];
    int n = snprintf(buf, sizeof buf,
                     "HTTP/1.1 %d OK\r\n"
                     "Content-Type: application/json\r\n"
                     "Content-Length: %zu\r\n"
                     "Connection: close\r\n"
                     "\r\n"
                     "%s",
                     status, strlen(body), body);
    if (n > 0) {
        send(fd, buf, (size_t)n, MSG_NOSIGNAL);
    }
}

static void *serve(void *arg) {
    fake_registry *srv = arg;
    while (!atomic_load(&srv->stop)) {
        struct pollfd pfd;
        int client;
        char req[2048];
        size_t used = 0;
        pfd.fd = srv->fd;
        pfd.events = POLLIN;
        if (poll(&pfd, 1, 100) <= 0) {
            continue;
        }
        client = accept(srv->fd, NULL, NULL);
        if (client < 0) {
            continue;
        }
        req[0] = '\0';
        while (used + 1 < sizeof req) {
            ssize_t n = recv(client, req + used, sizeof req - 1 - used, 0);
            char *split;
            if (n <= 0) {
                break;
            }
            used += (size_t)n;
            req[used] = '\0';
            split = strstr(req, "\r\n\r\n");
            if (!split) {
                continue;
            }
            if (strncmp(req, "POST /api/metrics", 17) == 0) {
                snprintf(srv->last_body, sizeof srv->last_body, "%s", split + 4);
                srv->posts++;
                respond(client, 201, "{\"id\":1}");
            } else if (strncmp(req, "GET /api/devices", 16) == 0) {
                respond(client, 200,
                        "{\"devices\":[{\"id\":3,\"name\":\"lab\",\"host\":\"127.0.0.1\","
                        "\"icmp_enabled\":false,\"tcp_port\":9}]}");
            } else {
                respond(client, 404, "{\"error\":\"not found\"}");
            }
            break;
        }
        close(client);
    }
    return NULL;
}

static int start_fake(fake_registry *srv) {
    struct sockaddr_in addr;
    socklen_t len = sizeof addr;
    memset(srv, 0, sizeof *srv);
    srv->fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (srv->fd < 0) {
        return -1;
    }
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(srv->fd, (struct sockaddr *)&addr, sizeof addr) < 0 || listen(srv->fd, 8) < 0) {
        close(srv->fd);
        return -1;
    }
    if (getsockname(srv->fd, (struct sockaddr *)&addr, &len) < 0) {
        close(srv->fd);
        return -1;
    }
    srv->port = ntohs(addr.sin_port);
    atomic_store(&srv->stop, 0);
    if (pthread_create(&srv->thread, NULL, serve, srv) != 0) {
        close(srv->fd);
        return -1;
    }
    return 0;
}

static void stop_fake(fake_registry *srv) {
    atomic_store(&srv->stop, 1);
    shutdown(srv->fd, SHUT_RDWR);
    pthread_join(srv->thread, NULL);
    close(srv->fd);
}

int test_http(void) {
    char host[64];
    int port = 0;
    fake_registry srv;
    np_device *devices = NULL;
    size_t count = 0;
    char err[256];
    char url[128];
    np_metric metric;
    int status = 0;
    char *body = NULL;

    EXPECT(http_parse_origin("http://registry:5000", host, sizeof host, &port) == 0);
    EXPECT(strcmp(host, "registry") == 0);
    EXPECT(port == 5000);
    EXPECT(http_parse_origin("http://127.0.0.1", host, sizeof host, &port) == 0);
    EXPECT(port == 80);
    EXPECT(http_parse_origin("https://example.com", host, sizeof host, &port) != 0);

    EXPECT(start_fake(&srv) == 0);
    snprintf(url, sizeof url, "http://127.0.0.1:%d", srv.port);
    EXPECT(registry_fetch_devices(url, 1000, &devices, &count, err, sizeof err) == 0);
    EXPECT(count == 1);
    EXPECT(devices && devices[0].id == 3);
    EXPECT(devices && devices[0].tcp_port == 9);
    EXPECT(devices && devices[0].icmp_enabled == 0);
    devices_free(devices);

    memset(&metric, 0, sizeof metric);
    metric.device_id = 3;
    snprintf(metric.device_name, sizeof metric.device_name, "lab");
    snprintf(metric.host, sizeof metric.host, "127.0.0.1");
    snprintf(metric.check, sizeof metric.check, "tcp");
    metric.ok = 1;
    metric.latency_ms = 0.5;
    snprintf(metric.detail, sizeof metric.detail, "connected");
    snprintf(metric.observed_at, sizeof metric.observed_at, "2026-11-25T00:00:00Z");
    EXPECT(registry_post_metric(url, &metric, 1000) == 0);
    EXPECT(srv.posts == 1);
    EXPECT(strstr(srv.last_body, "\"check\":\"tcp\"") != NULL);

    EXPECT(http_request("127.0.0.1", srv.port, "GET", "/missing", NULL, NULL, 1000,
                        &status, &body, NULL, err, sizeof err) == 0);
    EXPECT(status == 404);
    free(body);
    stop_fake(&srv);
    return 0;
}
