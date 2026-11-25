#include "checks.h"
#include "log.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <stdatomic.h>

#define ICMP_ECHO_REQUEST 8
#define ICMP_ECHO_REPLY 0
#define ICMP_PAYLOAD 32

enum {
    ICMP_KIND_NONE = 0,
    ICMP_KIND_DGRAM = 1,
    ICMP_KIND_RAW = 2
};

uint16_t internet_checksum(const void *data, size_t len) {
    const uint8_t *bytes = data;
    uint32_t sum = 0;
    while (len > 1) {
        uint16_t word;
        memcpy(&word, bytes, sizeof word);
        sum += word;
        bytes += 2;
        len -= 2;
    }
    if (len == 1) {
        sum += *bytes;
    }
    while (sum >> 16) {
        sum = (sum & 0xffffu) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

static double elapsed_ms(const struct timespec *start) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)(now.tv_sec - start->tv_sec) * 1000.0 +
           (double)(now.tv_nsec - start->tv_nsec) / 1e6;
}

static void note_socket(int kind, int errsave) {
    static int noted = 0;
    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&mu);
    if (!noted) {
        noted = 1;
        if (kind == ICMP_KIND_DGRAM) {
            log_msg(NP_LOG_INFO, "icmp", "using unprivileged ICMP datagram sockets");
        } else if (kind == ICMP_KIND_RAW) {
            log_msg(NP_LOG_INFO, "icmp", "using raw ICMP sockets");
        } else {
            log_msg(NP_LOG_WARN, "icmp", "ICMP socket unavailable: %s", strerror(errsave));
        }
    }
    pthread_mutex_unlock(&mu);
}

/*
 * Datagram ICMP sockets are allowed when ping_group_range includes this
 * process. Raw sockets need CAP_NET_RAW. The kernel owns the echo identifier
 * on a datagram socket and fills the checksum, so those fields stay zero.
 */
static int open_icmp(int *kind) {
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, IPPROTO_ICMP);
    if (fd >= 0) {
        *kind = ICMP_KIND_DGRAM;
        note_socket(*kind, 0);
        return fd;
    }
    fd = socket(AF_INET, SOCK_RAW | SOCK_CLOEXEC, IPPROTO_ICMP);
    if (fd >= 0) {
        *kind = ICMP_KIND_RAW;
        note_socket(*kind, 0);
        return fd;
    }
    *kind = ICMP_KIND_NONE;
    note_socket(*kind, errno);
    return -1;
}

static int resolve_ipv4(const char *host, struct sockaddr_in *dest, char *detail, size_t detail_len) {
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    int rc;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
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
    dest->sin_port = 0;
    return 0;
}

void icmp_probe(const char *host, int timeout_ms, np_metric *metric) {
    struct timespec start;
    struct sockaddr_in dest;
    uint8_t packet[8 + ICMP_PAYLOAD];
    uint8_t marker[4] = {'N', 'P', 'v', '1'};
    unsigned seq;
    int fd = -1;
    int kind = ICMP_KIND_NONE;
    static atomic_uint seq_gen = 1;

    metric->ok = 0;
    metric->latency_ms = 0.0;
    metric->detail[0] = '\0';
    if (!host || host[0] == '\0') {
        snprintf(metric->detail, sizeof metric->detail, "missing host");
        return;
    }
    if (resolve_ipv4(host, &dest, metric->detail, sizeof metric->detail) != 0) {
        return;
    }
    fd = open_icmp(&kind);
    if (fd < 0) {
        snprintf(metric->detail, sizeof metric->detail, "icmp socket: %s", strerror(errno));
        return;
    }

    seq = atomic_fetch_add(&seq_gen, 1) & 0xffffu;
    if (seq == 0) {
        seq = atomic_fetch_add(&seq_gen, 1) & 0xffffu;
    }
    memset(packet, 0, sizeof packet);
    packet[0] = ICMP_ECHO_REQUEST;
    if (kind == ICMP_KIND_RAW) {
        uint16_t id = (uint16_t)(getpid() & 0xffff);
        uint16_t id_net;
        uint16_t seq_net;
        uint16_t csum;
        if (id == 0) {
            id = 1;
        }
        id_net = htons(id);
        seq_net = htons((uint16_t)seq);
        memcpy(packet + 4, &id_net, sizeof id_net);
        memcpy(packet + 6, &seq_net, sizeof seq_net);
        memcpy(packet + 8, marker, sizeof marker);
        memcpy(packet + 12, &seq_net, sizeof seq_net);
        csum = internet_checksum(packet, sizeof packet);
        memcpy(packet + 2, &csum, sizeof csum);
    } else {
        uint16_t seq_net = htons((uint16_t)seq);
        memcpy(packet + 6, &seq_net, sizeof seq_net);
        memcpy(packet + 8, marker, sizeof marker);
        memcpy(packet + 12, &seq_net, sizeof seq_net);
    }

    clock_gettime(CLOCK_MONOTONIC, &start);
    if (sendto(fd, packet, sizeof packet, 0, (struct sockaddr *)&dest, sizeof dest) < 0) {
        snprintf(metric->detail, sizeof metric->detail, "send: %s", strerror(errno));
        metric->latency_ms = elapsed_ms(&start);
        close(fd);
        return;
    }

    for (;;) {
        struct pollfd pfd;
        uint8_t buf[1500];
        struct sockaddr_in from;
        socklen_t from_len = sizeof from;
        ssize_t n;
        int waited = (int)elapsed_ms(&start);
        int remain;
        int ip_off = 0;
        uint16_t got_seq = 0;
        if (waited >= timeout_ms) {
            snprintf(metric->detail, sizeof metric->detail, "timeout");
            metric->latency_ms = elapsed_ms(&start);
            break;
        }
        remain = timeout_ms - waited;
        pfd.fd = fd;
        pfd.events = POLLIN;
        n = poll(&pfd, 1, remain);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            snprintf(metric->detail, sizeof metric->detail, "poll: %s", strerror(errno));
            metric->latency_ms = elapsed_ms(&start);
            break;
        }
        if (n == 0) {
            snprintf(metric->detail, sizeof metric->detail, "timeout");
            metric->latency_ms = elapsed_ms(&start);
            break;
        }
        n = recvfrom(fd, buf, sizeof buf, 0, (struct sockaddr *)&from, &from_len);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            snprintf(metric->detail, sizeof metric->detail, "recv: %s", strerror(errno));
            metric->latency_ms = elapsed_ms(&start);
            break;
        }
        if (kind == ICMP_KIND_RAW) {
            if (n < 20) {
                continue;
            }
            ip_off = (buf[0] & 0x0f) * 4;
        }
        if (n < ip_off + 8 + 4) {
            continue;
        }
        if (buf[ip_off] != ICMP_ECHO_REPLY) {
            continue;
        }
        memcpy(&got_seq, buf + ip_off + 6, sizeof got_seq);
        if (ntohs(got_seq) != (uint16_t)seq) {
            continue;
        }
        if (memcmp(buf + ip_off + 8, marker, sizeof marker) != 0) {
            continue;
        }
        if (from.sin_addr.s_addr != dest.sin_addr.s_addr) {
            continue;
        }
        metric->ok = 1;
        metric->latency_ms = elapsed_ms(&start);
        snprintf(metric->detail, sizeof metric->detail, "echo reply");
        break;
    }
    close(fd);
}
