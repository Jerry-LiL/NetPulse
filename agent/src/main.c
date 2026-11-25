#include "checks.h"
#include "config.h"
#include "health.h"
#include "log.h"
#include "poller.h"
#include "signals.h"

#include <stdio.h>
#include <string.h>

static void usage(void) {
    fprintf(stderr,
            "usage: netpulse [--run-once]\n"
            "\n"
            "Reads NETPULSE_* from the environment. Metrics are JSON lines on\n"
            "stdout. Logs are JSON lines on stderr. GET /health and GET /ready\n"
            "are served on NETPULSE_HEALTH_PORT.\n");
}

int main(int argc, char **argv) {
    np_config cfg;
    poller *agent;
    health_server *health = NULL;
    int bound = 0;
    int rc;

    if (argc > 1 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        usage();
        return 0;
    }
    if (config_load(&cfg) != 0) {
        return 2;
    }
    if (argc > 1) {
        if (strcmp(argv[1], "--run-once") == 0) {
            cfg.run_once = 1;
        } else {
            usage();
            return 2;
        }
    }
    log_init(cfg.log_level);
    if (cfg.registry_url[0] == '\0' && cfg.devices_file[0] == '\0') {
        log_msg(NP_LOG_ERROR, "agent",
                "set NETPULSE_REGISTRY_URL or NETPULSE_DEVICES_FILE");
        return 2;
    }
    signals_install();
    agent = poller_new(&cfg);
    if (!agent) {
        log_msg(NP_LOG_ERROR, "agent", "out of memory");
        return 1;
    }
    snmp_client_init();
    if (cfg.health_port > 0) {
        health = health_start(cfg.health_addr, cfg.health_port, &bound,
                              poller_fill_snapshot, agent);
        if (!health) {
            log_msg(NP_LOG_ERROR, "health", "could not listen on %s:%d",
                    cfg.health_addr, cfg.health_port);
            poller_free(agent);
            return 1;
        }
        log_msg(NP_LOG_INFO, "health", "listening on %s:%d", cfg.health_addr, bound);
    }
    rc = poller_run(agent);
    health_stop(health);
    poller_free(agent);
    snmp_client_shutdown();
    return rc == 0 ? 0 : 1;
}
