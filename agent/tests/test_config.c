#include "config.h"
#include "harness.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void clear_env(void) {
    unsetenv("NETPULSE_REGISTRY_URL");
    unsetenv("NETPULSE_DEVICES_FILE");
    unsetenv("NETPULSE_HEALTH_ADDR");
    unsetenv("NETPULSE_POLL_INTERVAL");
    unsetenv("NETPULSE_WORKERS");
    unsetenv("NETPULSE_HEALTH_PORT");
    unsetenv("NETPULSE_CHECK_TIMEOUT_MS");
    unsetenv("NETPULSE_LOG_LEVEL");
    unsetenv("NETPULSE_RUN_ONCE");
}

static int load_quiet(np_config *cfg) {
    int saved = dup(STDERR_FILENO);
    int devnull = open("/dev/null", O_WRONLY);
    int rc;
    if (saved < 0 || devnull < 0) {
        if (saved >= 0) {
            close(saved);
        }
        if (devnull >= 0) {
            close(devnull);
        }
        return config_load(cfg);
    }
    dup2(devnull, STDERR_FILENO);
    close(devnull);
    rc = config_load(cfg);
    dup2(saved, STDERR_FILENO);
    close(saved);
    return rc;
}

int test_config(void) {
    np_config cfg;
    clear_env();
    EXPECT(config_load(&cfg) == 0);
    EXPECT(cfg.poll_interval_s == 15);
    EXPECT(cfg.workers == 4);
    EXPECT(cfg.health_port == 8080);
    EXPECT(cfg.check_timeout_ms == 2000);
    EXPECT(cfg.log_level == NP_LOG_INFO);
    EXPECT(cfg.run_once == 0);
    EXPECT(strcmp(cfg.health_addr, "0.0.0.0") == 0);

    setenv("NETPULSE_REGISTRY_URL", "http://127.0.0.1:5000", 1);
    setenv("NETPULSE_WORKERS", "2", 1);
    setenv("NETPULSE_LOG_LEVEL", "debug", 1);
    setenv("NETPULSE_RUN_ONCE", "yes", 1);
    setenv("NETPULSE_HEALTH_PORT", "0", 1);
    EXPECT(config_load(&cfg) == 0);
    EXPECT(strcmp(cfg.registry_url, "http://127.0.0.1:5000") == 0);
    EXPECT(cfg.workers == 2);
    EXPECT(cfg.log_level == NP_LOG_DEBUG);
    EXPECT(cfg.run_once == 1);
    EXPECT(cfg.health_port == 0);

    setenv("NETPULSE_WORKERS", "0", 1);
    EXPECT(load_quiet(&cfg) != 0);
    setenv("NETPULSE_WORKERS", "2", 1);
    setenv("NETPULSE_HEALTH_ADDR", "not-an-ip", 1);
    EXPECT(load_quiet(&cfg) != 0);
    setenv("NETPULSE_HEALTH_ADDR", "127.0.0.1", 1);
    setenv("NETPULSE_LOG_LEVEL", "loud", 1);
    EXPECT(load_quiet(&cfg) != 0);
    clear_env();
    return 0;
}
