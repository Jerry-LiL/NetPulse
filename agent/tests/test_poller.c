#include "harness.h"
#include "json_util.h"
#include "log.h"
#include "poller.h"

#include <arpa/inet.h>
#include <fcntl.h>
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
    atomic_int stop;
    pthread_t thread;
} acceptor;

static void *accept_loop(void *arg) {
    acceptor *acc = arg;
    while (!atomic_load(&acc->stop)) {
        struct pollfd pfd;
        int client;
        pfd.fd = acc->fd;
        pfd.events = POLLIN;
        if (poll(&pfd, 1, 50) <= 0) {
            continue;
        }
        client = accept(acc->fd, NULL, NULL);
        if (client >= 0) {
            close(client);
        }
    }
    return NULL;
}

int test_poller(void) {
    int listener;
    struct sockaddr_in addr;
    socklen_t len = sizeof addr;
    int port = 0;
    char devices_path[] = "/tmp/netpulse-poll-XXXXXX";
    char output_path[] = "/tmp/netpulse-out-XXXXXX";
    int devices_fd;
    int output_fd;
    int saved_stdout = -1;
    FILE *file;
    np_config cfg;
    poller *agent;
    acceptor acc;
    char line[2048];
    json_value *root;
    const json_value *item;
    char err[128];
    health_snapshot snap;

    listener = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    EXPECT(listener >= 0);
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    EXPECT(bind(listener, (struct sockaddr *)&addr, sizeof addr) == 0);
    EXPECT(listen(listener, 8) == 0);
    EXPECT(getsockname(listener, (struct sockaddr *)&addr, &len) == 0);
    port = ntohs(addr.sin_port);
    memset(&acc, 0, sizeof acc);
    acc.fd = listener;
    atomic_store(&acc.stop, 0);
    EXPECT(pthread_create(&acc.thread, NULL, accept_loop, &acc) == 0);

    devices_fd = mkstemp(devices_path);
    EXPECT(devices_fd >= 0);
    file = fdopen(devices_fd, "w");
    fprintf(file, "8|local|127.0.0.1|0|%d|0||\n", port);
    fclose(file);

    output_fd = mkstemp(output_path);
    EXPECT(output_fd >= 0);
    fflush(stdout);
    saved_stdout = dup(STDOUT_FILENO);
    EXPECT(dup2(output_fd, STDOUT_FILENO) >= 0);
    close(output_fd);

    memset(&cfg, 0, sizeof cfg);
    snprintf(cfg.devices_file, sizeof cfg.devices_file, "%s", devices_path);
    cfg.workers = 2;
    cfg.poll_interval_s = 15;
    cfg.check_timeout_ms = 1000;
    cfg.log_level = NP_LOG_ERROR;
    cfg.run_once = 1;
    log_init(NP_LOG_ERROR);
    agent = poller_new(&cfg);
    EXPECT(agent != NULL);
    EXPECT(poller_run(agent) == 0);
    poller_fill_snapshot(agent, &snap);
    EXPECT(snap.checks_total == 1);
    EXPECT(snap.checks_failed == 0);
    EXPECT(snap.ready == 1);
    EXPECT(snap.registry_state == -1);
    poller_free(agent);

    fflush(stdout);
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdout);

    file = fopen(output_path, "r");
    EXPECT(file != NULL);
    EXPECT(fgets(line, sizeof line, file) != NULL);
    fclose(file);
    root = json_parse(line, err, sizeof err);
    EXPECT(root != NULL);
    item = json_object_get(root, "device_id");
    EXPECT(item && item->number == 8);
    item = json_object_get(root, "check");
    EXPECT(item && strcmp(item->string, "tcp") == 0);
    item = json_object_get(root, "ok");
    EXPECT(item && item->type == JSON_BOOL && item->boolean == 1);
    item = json_object_get(root, "device");
    EXPECT(item && strcmp(item->string, "local") == 0);
    json_free(root);

    atomic_store(&acc.stop, 1);
    shutdown(listener, SHUT_RDWR);
    pthread_join(acc.thread, NULL);
    close(listener);
    unlink(devices_path);
    unlink(output_path);
    return 0;
}
