#ifndef NETPULSE_HTTP_CLIENT_H
#define NETPULSE_HTTP_CLIENT_H

#include <stddef.h>

/*
 * One HTTP/1.1 request over a POSIX socket. Only http:// is supported.
 * On success, *response_body is a malloc'd buffer the caller frees.
 * status_out receives the HTTP status code.
 */
int http_request(const char *host, int port, const char *method, const char *path,
                 const char *content_type, const char *body, int timeout_ms,
                 int *status_out, char **response_body, size_t *response_len,
                 char *err, size_t err_len);

/* Split http://host[:port][/ignored]. Returns 0 or -1. */
int http_parse_origin(const char *url, char *host, size_t host_len, int *port);

#endif
