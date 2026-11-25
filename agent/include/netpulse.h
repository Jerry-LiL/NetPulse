#ifndef NETPULSE_H
#define NETPULSE_H

#include <stddef.h>

#define NP_NAME_LEN 64
#define NP_HOST_LEN 256
#define NP_COMM_LEN 64
#define NP_OID_LEN 128
#define NP_DETAIL_LEN 256
#define NP_CHECK_LEN 16
#define NP_TIME_LEN 32

typedef struct {
    int id;
    char name[NP_NAME_LEN];
    char host[NP_HOST_LEN];
    int icmp_enabled;
    int tcp_port;
    int snmp_port;
    char snmp_community[NP_COMM_LEN];
    char snmp_oid[NP_OID_LEN];
} np_device;

typedef struct {
    int device_id;
    char device_name[NP_NAME_LEN];
    char host[NP_HOST_LEN];
    char check[NP_CHECK_LEN];
    int ok;
    double latency_ms;
    char detail[NP_DETAIL_LEN];
    char observed_at[NP_TIME_LEN];
} np_metric;

typedef enum {
    NP_LOG_ERROR = 0,
    NP_LOG_WARN = 1,
    NP_LOG_INFO = 2,
    NP_LOG_DEBUG = 3
} np_log_level;

typedef struct {
    char registry_url[512];
    char devices_file[512];
    int poll_interval_s;
    int workers;
    char health_addr[64];
    int health_port;
    int check_timeout_ms;
    np_log_level log_level;
    int run_once;
} np_config;

#endif
