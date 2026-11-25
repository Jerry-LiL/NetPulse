#include "checks.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static double elapsed_ms(const struct timespec *start) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)(now.tv_sec - start->tv_sec) * 1000.0 +
           (double)(now.tv_nsec - start->tv_nsec) / 1e6;
}

static int resolve_ipv4(const char *host, int port, struct sockaddr_in *dest,
                        char *detail, size_t detail_len) {
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    int rc;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    rc = getaddrinfo(host, NULL, &hints, &res);
    if (rc != 0 || !res) {
        snprintf(detail, detail_len, "dns: %s", gai_strerror(rc));
        if (res) {
            freeaddrinfo(res);
        }
        return -1;
    }
    memcpy(dest, res->ai_addr, sizeof(*dest));
    freeaddrinfo(res);
    dest->sin_port = htons((uint16_t)port);
    return 0;
}

void tcp_probe(const char *host, int port, int timeout_ms, np_metric *metric) {
    struct timespec start;
    struct sockaddr_in dest;
    int fd = -1;
    int rc;
    int flags;

    metric->ok = 0;
    metric->latency_ms = 0.0;
    metric->detail[0] = '\0';
    if (!host || host[0] == '\0') {
        snprintf(metric->detail, sizeof metric->detail, "missing host");
        return;
    }
    if (port < 1 || port > 65535) {
        snprintf(metric->detail, sizeof metric->detail, "tcp port is disabled");
        return;
    }
    if (resolve_ipv4(host, port, &dest, metric->detail, sizeof metric->detail) != 0) {
        return;
    }

    fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        snprintf(metric->detail, sizeof metric->detail, "socket: %s", strerror(errno));
        return;
    }
    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        snprintf(metric->detail, sizeof metric->detail, "fcntl: %s", strerror(errno));
        close(fd);
        return;
    }

    clock_gettime(CLOCK_MONOTONIC, &start);
    rc = connect(fd, (struct sockaddr *)&dest, sizeof dest);
    if (rc < 0 && errno != EINPROGRESS) {
        snprintf(metric->detail, sizeof metric->detail, "%s", strerror(errno));
        metric->latency_ms = elapsed_ms(&start);
        close(fd);
        return;
    }
    if (rc < 0) {
        struct pollfd pfd;
        int so_error = 0;
        socklen_t len = sizeof so_error;
        pfd.fd = fd;
        pfd.events = POLLOUT;
        rc = poll(&pfd, 1, timeout_ms);
        if (rc < 0) {
            snprintf(metric->detail, sizeof metric->detail, "poll: %s", strerror(errno));
            metric->latency_ms = elapsed_ms(&start);
            close(fd);
            return;
        }
        if (rc == 0) {
            snprintf(metric->detail, sizeof metric->detail, "timeout");
            metric->latency_ms = elapsed_ms(&start);
            close(fd);
            return;
        }
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len) < 0) {
            snprintf(metric->detail, sizeof metric->detail, "getsockopt: %s", strerror(errno));
            metric->latency_ms = elapsed_ms(&start);
            close(fd);
            return;
        }
        if (so_error != 0) {
            snprintf(metric->detail, sizeof metric->detail, "%s", strerror(so_error));
            metric->latency_ms = elapsed_ms(&start);
            close(fd);
            return;
        }
    }

    metric->ok = 1;
    metric->latency_ms = elapsed_ms(&start);
    snprintf(metric->detail, sizeof metric->detail, "connected");
    close(fd);
}
