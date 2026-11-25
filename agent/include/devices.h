#ifndef NETPULSE_DEVICES_H
#define NETPULSE_DEVICES_H

#include "netpulse.h"

#include <stddef.h>

/*
 * Pipe-separated devices file:
 *   id|name|host|icmp|tcp_port|snmp_port|community|oid
 * Blank lines and lines starting with '#' are ignored.
 * A zero tcp_port disables TCP. Empty community or oid disables SNMP.
 */
int devices_load_file(const char *path, np_device **out, size_t *count,
                      char *err, size_t err_len);

/* Parse {"devices":[...]} as returned by the registry. */
int devices_from_json(const char *text, np_device **out, size_t *count,
                      char *err, size_t err_len);

void devices_free(np_device *devices);

#endif
