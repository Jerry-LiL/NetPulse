#include "checks.h"
#include "log.h"

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static pthread_once_t g_snmp_once = PTHREAD_ONCE_INIT;
static int g_snmp_ready = 0;

static void snmp_setup(void) {
    /*
     * Keep the library from writing a persistent config under $HOME.
     * Workers share this process-wide init and then use snmp_sess_*,
     * which is safe to call from more than one thread.
     */
    netsnmp_ds_set_boolean(NETSNMP_DS_LIBRARY_ID, NETSNMP_DS_LIB_DONT_PERSIST_STATE, 1);
    netsnmp_ds_set_boolean(NETSNMP_DS_LIBRARY_ID, NETSNMP_DS_LIB_DISABLE_PERSISTENT_LOAD, 1);
    netsnmp_ds_set_boolean(NETSNMP_DS_LIBRARY_ID, NETSNMP_DS_LIB_DISABLE_PERSISTENT_SAVE, 1);
    netsnmp_ds_set_int(NETSNMP_DS_LIBRARY_ID, NETSNMP_DS_LIB_MIB_WARNINGS, 0);
    /* Numeric OIDs only. ":" clears the default MIB list, including on
       distributions that ship no MIB files. */
    setenv("MIBS", ":", 0);
    snmp_disable_log();
    init_snmp("netpulse");
    g_snmp_ready = 1;
}

void snmp_client_init(void) {
    pthread_once(&g_snmp_once, snmp_setup);
}

void snmp_client_shutdown(void) {
    if (g_snmp_ready) {
        snmp_shutdown("netpulse");
        g_snmp_ready = 0;
    }
}

static double elapsed_ms(const struct timespec *start) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)(now.tv_sec - start->tv_sec) * 1000.0 +
           (double)(now.tv_nsec - start->tv_nsec) / 1e6;
}

static void copy_detail(np_metric *metric, const char *text) {
    snprintf(metric->detail, sizeof metric->detail, "%s", text ? text : "");
}

void snmp_probe(const char *host, int port, const char *community, const char *oid_text,
                int timeout_ms, np_metric *metric) {
    struct timespec start;
    netsnmp_session session;
    void *sess = NULL;
    netsnmp_pdu *pdu = NULL;
    netsnmp_pdu *response = NULL;
    oid name[MAX_OID_LEN];
    size_t name_len = MAX_OID_LEN;
    char peer[NP_HOST_LEN + 16];
    int status;

    metric->ok = 0;
    metric->latency_ms = 0.0;
    metric->detail[0] = '\0';
    if (!host || host[0] == '\0') {
        copy_detail(metric, "missing host");
        return;
    }
    if (!community || community[0] == '\0' || !oid_text || oid_text[0] == '\0') {
        copy_detail(metric, "snmp community or oid is empty");
        return;
    }
    if (port < 1 || port > 65535) {
        port = 161;
    }

    snmp_client_init();
    if (!read_objid(oid_text, name, &name_len)) {
        copy_detail(metric, "bad oid");
        return;
    }

    snmp_sess_init(&session);
    snprintf(peer, sizeof peer, "%s:%d", host, port);
    session.peername = peer;
    session.version = SNMP_VERSION_2c;
    session.community = (u_char *)community;
    session.community_len = strlen(community);
    session.timeout = (long)timeout_ms * 1000L;
    session.retries = 0;

    sess = snmp_sess_open(&session);
    if (!sess) {
        copy_detail(metric, "snmp open failed");
        return;
    }
    pdu = snmp_pdu_create(SNMP_MSG_GET);
    if (!pdu) {
        snmp_sess_close(sess);
        copy_detail(metric, "out of memory");
        return;
    }
    snmp_add_null_var(pdu, name, name_len);

    clock_gettime(CLOCK_MONOTONIC, &start);
    status = snmp_sess_synch_response(sess, pdu, &response);
    pdu = NULL;
    metric->latency_ms = elapsed_ms(&start);

    if (status != STAT_SUCCESS || !response) {
        int clib_err = 0;
        int snmp_err = 0;
        char *errstr = NULL;
        snmp_sess_error(sess, &clib_err, &snmp_err, &errstr);
        if (status == STAT_TIMEOUT) {
            copy_detail(metric, "timeout");
        } else {
            snprintf(metric->detail, sizeof metric->detail, "snmp: %s",
                     errstr ? errstr : "request failed");
        }
    } else if (response->errstat != SNMP_ERR_NOERROR) {
        snprintf(metric->detail, sizeof metric->detail, "snmp: %s",
                 snmp_errstring(response->errstat));
    } else if (!response->variables) {
        copy_detail(metric, "empty response");
    } else if (response->variables->type == SNMP_NOSUCHOBJECT ||
               response->variables->type == SNMP_NOSUCHINSTANCE ||
               response->variables->type == SNMP_ENDOFMIBVIEW) {
        copy_detail(metric, "no such object");
    } else {
        char pretty[192];
        int wrote = snprint_value(pretty, sizeof pretty, response->variables->name,
                                  response->variables->name_length, response->variables);
        if (wrote < 0) {
            copy_detail(metric, "value received");
        } else {
            copy_detail(metric, pretty);
        }
        metric->ok = 1;
    }

    if (response) {
        snmp_free_pdu(response);
    }
    snmp_sess_close(sess);
}
