#include "config.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_int(const char *name, int fallback, int min, int max, int *out) {
    const char *value = getenv(name);
    char *end = NULL;
    long parsed;
    if (!value || value[0] == '\0') {
        *out = fallback;
        return 0;
    }
    errno = 0;
    parsed = strtol(value, &end, 10);
    if (end == value || (end && *end != '\0') || errno != 0 || parsed < min || parsed > max) {
        fprintf(stderr, "netpulse: %s must be an integer from %d to %d\n", name, min, max);
        return -1;
    }
    *out = (int)parsed;
    return 0;
}

static int truthy(const char *value) {
    return value && (strcmp(value, "1") == 0 || strcmp(value, "true") == 0 ||
                     strcmp(value, "yes") == 0);
}

static np_log_level parse_level(const char *value) {
    if (!value || value[0] == '\0' || strcmp(value, "info") == 0) {
        return NP_LOG_INFO;
    }
    if (strcmp(value, "error") == 0) {
        return NP_LOG_ERROR;
    }
    if (strcmp(value, "warn") == 0 || strcmp(value, "warning") == 0) {
        return NP_LOG_WARN;
    }
    if (strcmp(value, "debug") == 0) {
        return NP_LOG_DEBUG;
    }
    return (np_log_level)-1;
}

static void copy_env(char *dst, size_t dst_len, const char *name) {
    const char *value = getenv(name);
    if (!value) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, dst_len, "%s", value);
}

int config_load(np_config *cfg) {
    const char *level_text;
    struct in_addr addr;
    memset(cfg, 0, sizeof(*cfg));
    copy_env(cfg->registry_url, sizeof cfg->registry_url, "NETPULSE_REGISTRY_URL");
    copy_env(cfg->devices_file, sizeof cfg->devices_file, "NETPULSE_DEVICES_FILE");
    copy_env(cfg->health_addr, sizeof cfg->health_addr, "NETPULSE_HEALTH_ADDR");
    if (cfg->health_addr[0] == '\0') {
        snprintf(cfg->health_addr, sizeof cfg->health_addr, "0.0.0.0");
    }
    if (inet_pton(AF_INET, cfg->health_addr, &addr) != 1) {
        fprintf(stderr, "netpulse: NETPULSE_HEALTH_ADDR must be an IPv4 address\n");
        return -1;
    }
    if (parse_int("NETPULSE_POLL_INTERVAL", 15, 1, 3600, &cfg->poll_interval_s) != 0 ||
        parse_int("NETPULSE_WORKERS", 4, 1, 32, &cfg->workers) != 0 ||
        parse_int("NETPULSE_HEALTH_PORT", 8080, 0, 65535, &cfg->health_port) != 0 ||
        parse_int("NETPULSE_CHECK_TIMEOUT_MS", 2000, 100, 30000, &cfg->check_timeout_ms) != 0) {
        return -1;
    }
    level_text = getenv("NETPULSE_LOG_LEVEL");
    cfg->log_level = parse_level(level_text);
    if ((int)cfg->log_level < 0) {
        fprintf(stderr, "netpulse: NETPULSE_LOG_LEVEL must be error, warn, info, or debug\n");
        return -1;
    }
    cfg->run_once = truthy(getenv("NETPULSE_RUN_ONCE"));
    return 0;
}
