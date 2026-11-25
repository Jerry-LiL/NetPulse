#include "http_client.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

static void set_err(char *err, size_t err_len, const char *msg) {
    if (err && err_len) {
        snprintf(err, err_len, "%s", msg);
    }
}

int http_parse_origin(const char *url, char *host, size_t host_len, int *port) {
    const char *start;
    const char *colon;
    const char *slash;
    size_t host_n;
    if (!url || strncmp(url, "http://", 7) != 0) {
        return -1;
    }
    start = url + 7;
    if (*start == '\0' || *start == '/' || *start == ':') {
        return -1;
    }
    slash = strchr(start, '/');
    colon = strchr(start, ':');
    if (colon && slash && colon > slash) {
        colon = NULL;
    }
    if (colon) {
        char *end = NULL;
        long parsed;
        host_n = (size_t)(colon - start);
        parsed = strtol(colon + 1, &end, 10);
        if (end == colon + 1 || parsed < 1 || parsed > 65535) {
            return -1;
        }
        if (slash) {
            if (end != slash) {
                return -1;
            }
        } else if (*end != '\0') {
            return -1;
        }
        *port = (int)parsed;
    } else {
        host_n = slash ? (size_t)(slash - start) : strlen(start);
        *port = 80;
    }
    if (host_n == 0 || host_n >= host_len) {
        return -1;
    }
    memcpy(host, start, host_n);
    host[host_n] = '\0';
    return 0;
}

static int connect_timeout(int fd, const struct sockaddr *addr, socklen_t addr_len, int timeout_ms,
                           char *err, size_t err_len) {
    int flags;
    int rc;
    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        set_err(err, err_len, strerror(errno));
        return -1;
    }
    rc = connect(fd, addr, addr_len);
    if (rc < 0 && errno != EINPROGRESS) {
        set_err(err, err_len, strerror(errno));
        return -1;
    }
    if (rc < 0) {
        struct pollfd pfd;
        int so_error = 0;
        socklen_t len = sizeof so_error;
        pfd.fd = fd;
        pfd.events = POLLOUT;
        rc = poll(&pfd, 1, timeout_ms);
        if (rc <= 0) {
            set_err(err, err_len, rc == 0 ? "timeout" : strerror(errno));
            return -1;
        }
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len) < 0 || so_error != 0) {
            set_err(err, err_len, strerror(so_error ? so_error : errno));
            return -1;
        }
    }
    if (fcntl(fd, F_SETFL, flags) < 0) {
        set_err(err, err_len, strerror(errno));
        return -1;
    }
    return 0;
}

static int send_all(int fd, const char *data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, data + sent, len - sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

static char *header_value(const char *headers, const char *name) {
    size_t name_len = strlen(name);
    const char *p = headers;
    while (*p) {
        const char *line_end = strstr(p, "\r\n");
        size_t line_len = line_end ? (size_t)(line_end - p) : strlen(p);
        if (line_len > name_len + 1 && strncasecmp(p, name, name_len) == 0 && p[name_len] == ':') {
            const char *value = p + name_len + 1;
            while (*value == ' ' || *value == '\t') {
                value++;
            }
            return strndup(value, (size_t)((line_end ? line_end : p + line_len) - value));
        }
        if (!line_end) {
            break;
        }
        p = line_end + 2;
        if (p[0] == '\r' && p[1] == '\n') {
            break;
        }
    }
    return NULL;
}

int http_request(const char *host, int port, const char *method, const char *path,
                 const char *content_type, const char *body, int timeout_ms,
                 int *status_out, char **response_body, size_t *response_len,
                 char *err, size_t err_len) {
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    char port_text[16];
    int fd = -1;
    int rc;
    char *request = NULL;
    size_t request_len = 0;
    char *raw = NULL;
    size_t raw_len = 0;
    char *split;
    char *content_length_text = NULL;
    long content_length = -1;
    int status = 0;
    const size_t max_body = 1024 * 1024;

    *response_body = NULL;
    if (response_len) {
        *response_len = 0;
    }
    if (!host || !method || !path || path[0] != '/') {
        set_err(err, err_len, "bad http request");
        return -1;
    }
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(port_text, sizeof port_text, "%d", port);
    rc = getaddrinfo(host, port_text, &hints, &res);
    if (rc != 0 || !res) {
        snprintf(err, err_len, "dns: %s", gai_strerror(rc));
        if (res) {
            freeaddrinfo(res);
        }
        return -1;
    }
    fd = socket(res->ai_family, res->ai_socktype | SOCK_CLOEXEC, res->ai_protocol);
    if (fd < 0) {
        set_err(err, err_len, strerror(errno));
        freeaddrinfo(res);
        return -1;
    }
    {
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    }
    if (connect_timeout(fd, res->ai_addr, res->ai_addrlen, timeout_ms, err, err_len) != 0) {
        freeaddrinfo(res);
        close(fd);
        return -1;
    }
    freeaddrinfo(res);

    if (body) {
        request_len = (size_t)snprintf(NULL, 0,
                                       "%s %s HTTP/1.1\r\n"
                                       "Host: %s:%d\r\n"
                                       "User-Agent: netpulse-agent\r\n"
                                       "Content-Type: %s\r\n"
                                       "Content-Length: %zu\r\n"
                                       "Connection: close\r\n"
                                       "\r\n",
                                       method, path, host, port,
                                       content_type ? content_type : "application/octet-stream",
                                       strlen(body));
        request = malloc(request_len + strlen(body) + 1);
        if (!request) {
            set_err(err, err_len, "out of memory");
            close(fd);
            return -1;
        }
        snprintf(request, request_len + 1,
                 "%s %s HTTP/1.1\r\n"
                 "Host: %s:%d\r\n"
                 "User-Agent: netpulse-agent\r\n"
                 "Content-Type: %s\r\n"
                 "Content-Length: %zu\r\n"
                 "Connection: close\r\n"
                 "\r\n",
                 method, path, host, port,
                 content_type ? content_type : "application/octet-stream",
                 strlen(body));
        memcpy(request + request_len, body, strlen(body));
        request_len += strlen(body);
    } else {
        request_len = (size_t)snprintf(NULL, 0,
                                       "%s %s HTTP/1.1\r\n"
                                       "Host: %s:%d\r\n"
                                       "User-Agent: netpulse-agent\r\n"
                                       "Accept: application/json\r\n"
                                       "Connection: close\r\n"
                                       "\r\n",
                                       method, path, host, port);
        request = malloc(request_len + 1);
        if (!request) {
            set_err(err, err_len, "out of memory");
            close(fd);
            return -1;
        }
        snprintf(request, request_len + 1,
                 "%s %s HTTP/1.1\r\n"
                 "Host: %s:%d\r\n"
                 "User-Agent: netpulse-agent\r\n"
                 "Accept: application/json\r\n"
                 "Connection: close\r\n"
                 "\r\n",
                 method, path, host, port);
    }
    rc = send_all(fd, request, request_len);
    free(request);
    if (rc != 0) {
        set_err(err, err_len, strerror(errno));
        close(fd);
        return -1;
    }

    for (;;) {
        char chunk[4096];
        ssize_t n = recv(fd, chunk, sizeof chunk, 0);
        char *next;
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            set_err(err, err_len, strerror(errno));
            free(raw);
            close(fd);
            return -1;
        }
        if (n == 0) {
            break;
        }
        if (raw_len + (size_t)n + 1 > max_body + 8192) {
            set_err(err, err_len, "http response is too large");
            free(raw);
            close(fd);
            return -1;
        }
        next = realloc(raw, raw_len + (size_t)n + 1);
        if (!next) {
            set_err(err, err_len, "out of memory");
            free(raw);
            close(fd);
            return -1;
        }
        raw = next;
        memcpy(raw + raw_len, chunk, (size_t)n);
        raw_len += (size_t)n;
        raw[raw_len] = '\0';
    }
    close(fd);
    if (!raw) {
        set_err(err, err_len, "empty http response");
        return -1;
    }
    split = strstr(raw, "\r\n\r\n");
    if (!split) {
        set_err(err, err_len, "malformed http response");
        free(raw);
        return -1;
    }
    {
        const char *space = strchr(raw, ' ');
        if (!space || sscanf(space + 1, "%d", &status) != 1) {
            set_err(err, err_len, "malformed http response");
            free(raw);
            return -1;
        }
    }
    split[0] = '\0';
    content_length_text = header_value(raw, "Content-Length");
    if (content_length_text) {
        char *end = NULL;
        content_length = strtol(content_length_text, &end, 10);
        if (end == content_length_text || content_length < 0) {
            content_length = -1;
        }
        free(content_length_text);
    }
    split += 4;
    raw_len = strlen(split);
    if (content_length >= 0 && (size_t)content_length < raw_len) {
        raw_len = (size_t)content_length;
        split[raw_len] = '\0';
    }
    *status_out = status;
    *response_body = strdup(split);
    free(raw);
    if (!*response_body) {
        set_err(err, err_len, "out of memory");
        return -1;
    }
    if (response_len) {
        *response_len = raw_len;
    }
    return 0;
}
