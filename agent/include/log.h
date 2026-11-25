#ifndef NETPULSE_LOG_H
#define NETPULSE_LOG_H

#include "netpulse.h"

#include <stddef.h>

void log_init(np_log_level level);
void log_msg(np_log_level level, const char *component, const char *fmt, ...);
void np_format_time(char *dst, size_t dst_len);

/* Format one log line without writing it. Used by tests. */
int log_format_line(np_log_level level, const char *component, const char *msg,
                    char *dst, size_t dst_len);

#endif
