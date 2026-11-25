#ifndef NETPULSE_CHECKS_H
#define NETPULSE_CHECKS_H

#include "netpulse.h"

#include <stddef.h>
#include <stdint.h>

uint16_t internet_checksum(const void *data, size_t len);

/*
 * Probes fill ok, latency_ms and detail. Identity fields are left alone.
 * A down host is a completed check (ok == 0), not a probe crash.
 */
void icmp_probe(const char *host, int timeout_ms, np_metric *metric);
void tcp_probe(const char *host, int port, int timeout_ms, np_metric *metric);
void snmp_probe(const char *host, int port, const char *community, const char *oid,
                int timeout_ms, np_metric *metric);

/* Prepare the net-snmp library. Safe to call more than once. */
void snmp_client_init(void);

/* Release library state allocated by the SNMP client. Safe to call once. */
void snmp_client_shutdown(void);

#endif
