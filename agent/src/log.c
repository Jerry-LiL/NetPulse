#include "log.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static np_log_level g_level = NP_LOG_INFO;
static pthread_mutex_t g_log_mu = PTHREAD_MUTEX_INITIALIZER;

void log_init(np_log_level level) {
    g_level = level;
}

void np_format_time(char *dst, size_t dst_len) {
    time_t now = time(NULL);
    struct tm tm;
    if (dst_len == 0) {
        return;
    }
    gmtime_r(&now, &tm);
    if (strftime(dst, dst_len, "%Y-%m-%dT%H:%M:%SZ", &tm) == 0) {
        dst[0] = '\0';
    }
}

static const char *level_name(np_log_level level) {
    switch (level) {
    case NP_LOG_ERROR:
        return "error";
    case NP_LOG_WARN:
        return "warn";
    case NP_LOG_INFO:
        return "info";
    case NP_LOG_DEBUG:
        return "debug";
    }
    return "info";
}

static int append_raw(char *dst, size_t dst_len, size_t *used, const char *src) {
    size_t n = strlen(src);
    if (*used + n >= dst_len) {
        return -1;
    }
    memcpy(dst + *used, src, n);
    *used += n;
    dst[*used] = '\0';
    return 0;
}

static int append_escaped(char *dst, size_t dst_len, size_t *used, const char *src) {
    const unsigned char *p = (const unsigned char *)src;
    if (append_raw(dst, dst_len, used, "\"") != 0) {
        return -1;
    }
    for (; *p; p++) {
        char tmp[8];
        const char *piece;
        if (*p == '"' || *p == '\\') {
            tmp[0] = '\\';
            tmp[1] = (char)*p;
            tmp[2] = '\0';
            piece = tmp;
        } else if (*p == '\n') {
            piece = "\\n";
        } else if (*p == '\r') {
            piece = "\\r";
        } else if (*p == '\t') {
            piece = "\\t";
        } else if (*p < 0x20) {
            snprintf(tmp, sizeof tmp, "\\u%04x", *p);
            piece = tmp;
        } else {
            tmp[0] = (char)*p;
            tmp[1] = '\0';
            piece = tmp;
        }
        if (append_raw(dst, dst_len, used, piece) != 0) {
            return -1;
        }
    }
    return append_raw(dst, dst_len, used, "\"");
}

int log_format_line(np_log_level level, const char *component, const char *msg,
                    char *dst, size_t dst_len) {
    char ts[NP_TIME_LEN];
    size_t used = 0;
    if (!component) {
        component = "agent";
    }
    if (!msg) {
        msg = "";
    }
    np_format_time(ts, sizeof ts);
    dst[0] = '\0';
    if (append_raw(dst, dst_len, &used, "{\"ts\":") != 0 ||
        append_escaped(dst, dst_len, &used, ts) != 0 ||
        append_raw(dst, dst_len, &used, ",\"level\":") != 0 ||
        append_escaped(dst, dst_len, &used, level_name(level)) != 0 ||
        append_raw(dst, dst_len, &used, ",\"component\":") != 0 ||
        append_escaped(dst, dst_len, &used, component) != 0 ||
        append_raw(dst, dst_len, &used, ",\"msg\":") != 0 ||
        append_escaped(dst, dst_len, &used, msg) != 0 ||
        append_raw(dst, dst_len, &used, "}") != 0) {
        return -1;
    }
    return 0;
}

void log_msg(np_log_level level, const char *component, const char *fmt, ...) {
    char msg[768];
    char line[1400];
    va_list ap;
    int wrote;

    if (level > g_level) {
        return;
    }
    va_start(ap, fmt);
    wrote = vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    if (wrote < 0) {
        return;
    }
    if (log_format_line(level, component, msg, line, sizeof line) != 0) {
        return;
    }
    pthread_mutex_lock(&g_log_mu);
    fprintf(stderr, "%s\n", line);
    pthread_mutex_unlock(&g_log_mu);
}
