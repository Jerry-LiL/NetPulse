#include "health.h"

#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <stdatomic.h>

struct health_server {
    int fd;
    pthread_t thread;
    atomic_int stop;
    health_snapshot_fn fn;
    void *userdata;
    time_t started;
};

static int read_request(int fd, char *buf, size_t buf_len) {
    size_t used = 0;
    while (used + 1 < buf_len) {
        ssize_t n = recv(fd, buf + used, buf_len - 1 - used, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            break;
        }
        used += (size_t)n;
        buf[used] = '\0';
        if (strstr(buf, "\r\n\r\n")) {
            return 0;
        }
    }
    return strstr(buf, "\r\n\r\n") ? 0 : -1;
}

static void write_all(int fd, const char *data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, data + sent, len - sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return;
        }
        if (n == 0) {
            return;
        }
        sent += (size_t)n;
    }
}

static void respond(int fd, int status, const char *reason, const char *body) {
    char header[256];
    int n = snprintf(header, sizeof header,
                     "HTTP/1.1 %d %s\r\n"
                     "Content-Type: application/json\r\n"
                     "Content-Length: %zu\r\n"
                     "Connection: close\r\n"
                     "\r\n",
                     status, reason, strlen(body));
    if (n < 0 || (size_t)n >= sizeof header) {
        return;
    }
    write_all(fd, header, (size_t)n);
    write_all(fd, body, strlen(body));
}

static void handle_client(health_server *server, int fd) {
    char request[2048];
    char body[512];
    health_snapshot snap;
    const char *path;
    const char *reason;
    int status;
    long uptime;
    const char *registry;

    memset(&snap, 0, sizeof snap);
    request[0] = '\0';
    if (read_request(fd, request, sizeof request) != 0 || strncmp(request, "GET ", 4) != 0) {
        respond(fd, 405, "Method Not Allowed", "{\"error\":\"method not allowed\"}");
        return;
    }
    path = request + 4;
    if (strncmp(path, "/health", 7) == 0 && (path[7] == ' ' || path[7] == '?')) {
        status = 200;
        reason = "OK";
    } else if (strncmp(path, "/ready", 6) == 0 && (path[6] == ' ' || path[6] == '?')) {
        if (server->fn) {
            server->fn(server->userdata, &snap);
        }
        status = snap.ready ? 200 : 503;
        reason = snap.ready ? "OK" : "Service Unavailable";
    } else {
        respond(fd, 404, "Not Found", "{\"error\":\"not found\"}");
        return;
    }
    if (server->fn) {
        server->fn(server->userdata, &snap);
    }
    uptime = (long)(time(NULL) - server->started);
    if (uptime < 0) {
        uptime = 0;
    }
    if (snap.registry_state < 0) {
        registry = "disabled";
    } else if (snap.registry_state == 0) {
        registry = "down";
    } else {
        registry = "ok";
    }
    snprintf(body, sizeof body,
             "{\"status\":\"%s\",\"uptime_seconds\":%ld,\"checks_total\":%lu,"
             "\"checks_failed\":%lu,\"queue_depth\":%zu,\"registry\":\"%s\"}",
             status == 200 ? "ok" : "not_ready", uptime, snap.checks_total,
             snap.checks_failed, snap.queue_depth, registry);
    respond(fd, status, reason, body);
}

static void *health_main(void *arg) {
    health_server *server = arg;
    while (!atomic_load(&server->stop)) {
        struct pollfd pfd;
        int client;
        pfd.fd = server->fd;
        pfd.events = POLLIN;
        if (poll(&pfd, 1, 200) <= 0) {
            continue;
        }
        client = accept(server->fd, NULL, NULL);
        if (client < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            break;
        }
        handle_client(server, client);
        close(client);
    }
    return NULL;
}

health_server *health_start(const char *addr, int port, int *bound_port,
                            health_snapshot_fn fn, void *userdata) {
    health_server *server;
    struct sockaddr_in sin;
    socklen_t len = sizeof sin;
    int fd;
    int one = 1;

    if (bound_port) {
        *bound_port = 0;
    }
    fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return NULL;
    }
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    memset(&sin, 0, sizeof sin);
    sin.sin_family = AF_INET;
    sin.sin_port = htons((uint16_t)port);
    if (!addr || inet_pton(AF_INET, addr, &sin.sin_addr) != 1) {
        close(fd);
        return NULL;
    }
    if (bind(fd, (struct sockaddr *)&sin, sizeof sin) < 0 || listen(fd, 16) < 0) {
        close(fd);
        return NULL;
    }
    if (getsockname(fd, (struct sockaddr *)&sin, &len) == 0 && bound_port) {
        *bound_port = ntohs(sin.sin_port);
    }
    server = calloc(1, sizeof(*server));
    if (!server) {
        close(fd);
        return NULL;
    }
    server->fd = fd;
    server->fn = fn;
    server->userdata = userdata;
    server->started = time(NULL);
    atomic_store(&server->stop, 0);
    if (pthread_create(&server->thread, NULL, health_main, server) != 0) {
        close(fd);
        free(server);
        return NULL;
    }
    return server;
}

void health_stop(health_server *server) {
    if (!server) {
        return;
    }
    atomic_store(&server->stop, 1);
    shutdown(server->fd, SHUT_RDWR);
    pthread_join(server->thread, NULL);
    close(server->fd);
    free(server);
}
