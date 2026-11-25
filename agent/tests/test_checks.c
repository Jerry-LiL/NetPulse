#include "checks.h"
#include "harness.h"
#include "log.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int checksum_round_trip(void) {
    uint8_t packet[8] = {8, 0, 0, 0, 0, 1, 0, 1};
    uint16_t sum = internet_checksum(packet, sizeof packet);
    EXPECT(sum != 0);
    memcpy(packet + 2, &sum, sizeof sum);
    EXPECT(internet_checksum(packet, sizeof packet) == 0);
    packet[7] ^= 0xff;
    EXPECT(internet_checksum(packet, sizeof packet) != 0);
    return 0;
}

static int listen_loopback(int *port) {
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    struct sockaddr_in addr;
    socklen_t len = sizeof addr;
    int one = 1;
    if (fd < 0) {
        return -1;
    }
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0 || listen(fd, 8) < 0) {
        close(fd);
        return -1;
    }
    if (getsockname(fd, (struct sockaddr *)&addr, &len) < 0) {
        close(fd);
        return -1;
    }
    *port = ntohs(addr.sin_port);
    return fd;
}

static int reserve_udp_port(int *port) {
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    struct sockaddr_in addr;
    socklen_t len = sizeof addr;
    if (fd < 0) {
        return -1;
    }
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        close(fd);
        return -1;
    }
    if (getsockname(fd, (struct sockaddr *)&addr, &len) < 0) {
        close(fd);
        return -1;
    }
    *port = ntohs(addr.sin_port);
    close(fd);
    return 0;
}

static pid_t start_snmpd(int port, char *dir, size_t dir_len) {
    char conf[80];
    char pidfile[80];
    char address[64];
    int conf_fd;
    pid_t pid;
    FILE *file;
    snprintf(dir, dir_len, "/tmp/netpulse-snmp-XXXXXX");
    if (!mkdtemp(dir)) {
        return -1;
    }
    snprintf(conf, sizeof conf, "%s/snmpd.conf", dir);
    snprintf(pidfile, sizeof pidfile, "%s/snmpd.pid", dir);
    snprintf(address, sizeof address, "udp:127.0.0.1:%d", port);
    conf_fd = open(conf, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (conf_fd < 0) {
        return -1;
    }
    file = fdopen(conf_fd, "w");
    if (!file) {
        close(conf_fd);
        return -1;
    }
    fputs("rocommunity public 127.0.0.1\n", file);
    fclose(file);
    pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        int logfd = open("/tmp/netpulse-snmpd.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (logfd >= 0) {
            dup2(logfd, STDOUT_FILENO);
            dup2(logfd, STDERR_FILENO);
            close(logfd);
        }
        setenv("SNMP_PERSISTENT_DIR", dir, 1);
        setenv("SNMPCONFPATH", dir, 1);
        execl("/usr/sbin/snmpd", "snmpd", "-f", "-Lo", "-C", "-c", conf, "-p", pidfile, address,
              (char *)NULL);
        _exit(127);
    }
    return pid;
}

static void stop_snmpd(pid_t pid) {
    if (pid > 0) {
        kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
    }
}

int test_checks(void) {
    np_metric metric;
    int port = 0;
    int listener;
    int accepted = -1;
    pid_t snmpd = -1;
    char dir[64];
    int i;

    log_init(NP_LOG_ERROR);
    checksum_round_trip();

    memset(&metric, 0, sizeof metric);
    icmp_probe("no-such-host.invalid", 300, &metric);
    EXPECT(metric.ok == 0);
    EXPECT(strstr(metric.detail, "dns:") != NULL);

    memset(&metric, 0, sizeof metric);
    icmp_probe("127.0.0.1", 800, &metric);
    if (strncmp(metric.detail, "icmp socket:", 12) == 0) {
        SKIP("ICMP sockets are not permitted in this environment");
    } else {
        EXPECT(metric.ok == 1);
        EXPECT(strcmp(metric.detail, "echo reply") == 0);
        EXPECT(metric.latency_ms >= 0);
    }

    listener = listen_loopback(&port);
    EXPECT(listener >= 0);
    memset(&metric, 0, sizeof metric);
    tcp_probe("127.0.0.1", port, 1000, &metric);
    EXPECT(metric.ok == 1);
    EXPECT(strcmp(metric.detail, "connected") == 0);
    accepted = accept(listener, NULL, NULL);
    if (accepted >= 0) {
        close(accepted);
    }
    close(listener);

    memset(&metric, 0, sizeof metric);
    tcp_probe("127.0.0.1", 1, 1000, &metric);
    EXPECT(metric.ok == 0);
    EXPECT(strstr(metric.detail, "refused") != NULL || strstr(metric.detail, "denied") != NULL);

    memset(&metric, 0, sizeof metric);
    tcp_probe("not-a-real-host.invalid", 9, 500, &metric);
    EXPECT(metric.ok == 0);
    EXPECT(strstr(metric.detail, "dns:") != NULL);

    memset(&metric, 0, sizeof metric);
    snmp_probe("127.0.0.1", 161, "public", "not an oid", 500, &metric);
    EXPECT(metric.ok == 0);
    EXPECT(strcmp(metric.detail, "bad oid") == 0);

    if (reserve_udp_port(&port) != 0 || access("/usr/sbin/snmpd", X_OK) != 0) {
        SKIP("snmpd is not available");
        return 0;
    }
    snmpd = start_snmpd(port, dir, sizeof dir);
    EXPECT(snmpd > 0);
    memset(&metric, 0, sizeof metric);
    for (i = 0; i < 30; i++) {
        snmp_probe("127.0.0.1", port, "public", "1.3.6.1.2.1.1.1.0", 400, &metric);
        if (metric.ok) {
            break;
        }
        struct timespec ts = {0, 100000000L};
        nanosleep(&ts, NULL);
    }
    EXPECT(metric.ok == 1);
    EXPECT(strstr(metric.detail, "Linux") != NULL || strstr(metric.detail, "STRING") != NULL);
    stop_snmpd(snmpd);
    return 0;
}
