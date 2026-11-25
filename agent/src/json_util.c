#include "json_util.h"
#include "log.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *p;
    const char *end;
    char err[160];
} parser;

static void set_err(parser *p, const char *msg) {
    if (p->err[0] == '\0') {
        snprintf(p->err, sizeof p->err, "%s", msg);
    }
}

static void skip_ws(parser *p) {
    while (p->p < p->end) {
        char c = *p->p;
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
            break;
        }
        p->p++;
    }
}

static int grow(char **buf, size_t *len, size_t *cap, size_t extra) {
    size_t need;
    char *next;
    if (*len + extra + 1 <= *cap) {
        return 0;
    }
    need = *cap ? *cap : 64;
    while (*len + extra + 1 > need) {
        if (need > 1024 * 1024) {
            return -1;
        }
        need *= 2;
    }
    next = realloc(*buf, need);
    if (!next) {
        return -1;
    }
    *buf = next;
    *cap = need;
    return 0;
}

int json_append_string(char **dst, size_t *len, size_t *cap, const char *src) {
    const unsigned char *p;
    if (!src) {
        src = "";
    }
    if (grow(dst, len, cap, 1) != 0) {
        return -1;
    }
    (*dst)[(*len)++] = '"';
    for (p = (const unsigned char *)src; *p; p++) {
        char tmp[8];
        const char *piece = tmp;
        size_t n;
        if (*p == '"' || *p == '\\') {
            tmp[0] = '\\';
            tmp[1] = (char)*p;
            tmp[2] = '\0';
        } else if (*p == '\n') {
            piece = "\\n";
        } else if (*p == '\r') {
            piece = "\\r";
        } else if (*p == '\t') {
            piece = "\\t";
        } else if (*p < 0x20) {
            snprintf(tmp, sizeof tmp, "\\u%04x", *p);
        } else {
            tmp[0] = (char)*p;
            tmp[1] = '\0';
        }
        n = strlen(piece);
        if (grow(dst, len, cap, n) != 0) {
            return -1;
        }
        memcpy(*dst + *len, piece, n);
        *len += n;
    }
    if (grow(dst, len, cap, 1) != 0) {
        return -1;
    }
    (*dst)[(*len)++] = '"';
    (*dst)[*len] = '\0';
    return 0;
}

void json_free(json_value *value) {
    size_t i;
    if (!value) {
        return;
    }
    free(value->string);
    if (value->type == JSON_ARRAY || value->type == JSON_OBJECT) {
        for (i = 0; i < value->count; i++) {
            json_free(value->items[i]);
            if (value->keys) {
                free(value->keys[i]);
            }
        }
        free(value->items);
        free(value->keys);
    }
    free(value);
}

const json_value *json_object_get(const json_value *object, const char *key) {
    size_t i;
    if (!object || object->type != JSON_OBJECT || !key) {
        return NULL;
    }
    for (i = 0; i < object->count; i++) {
        if (strcmp(object->keys[i], key) == 0) {
            return object->items[i];
        }
    }
    return NULL;
}

static json_value *make_value(json_type type) {
    json_value *value = calloc(1, sizeof(*value));
    if (value) {
        value->type = type;
    }
    return value;
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static int append_utf8(char **buf, size_t *len, size_t *cap, unsigned int cp) {
    unsigned char tmp[4];
    size_t n = 0;
    if (cp <= 0x7f) {
        tmp[0] = (unsigned char)cp;
        n = 1;
    } else if (cp <= 0x7ff) {
        tmp[0] = (unsigned char)(0xc0 | (cp >> 6));
        tmp[1] = (unsigned char)(0x80 | (cp & 0x3f));
        n = 2;
    } else if (cp <= 0xffff) {
        tmp[0] = (unsigned char)(0xe0 | (cp >> 12));
        tmp[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3f));
        tmp[2] = (unsigned char)(0x80 | (cp & 0x3f));
        n = 3;
    } else {
        tmp[0] = (unsigned char)(0xf0 | (cp >> 18));
        tmp[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3f));
        tmp[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3f));
        tmp[3] = (unsigned char)(0x80 | (cp & 0x3f));
        n = 4;
    }
    if (grow(buf, len, cap, n) != 0) {
        return -1;
    }
    memcpy(*buf + *len, tmp, n);
    *len += n;
    (*buf)[*len] = '\0';
    return 0;
}

static int parse_hex4(parser *p, unsigned int *out) {
    int i;
    unsigned int cp = 0;
    for (i = 0; i < 4; i++) {
        int h;
        if (p->p >= p->end) {
            set_err(p, "truncated unicode escape");
            return -1;
        }
        h = hex_value(*p->p++);
        if (h < 0) {
            set_err(p, "bad unicode escape");
            return -1;
        }
        cp = (cp << 4) | (unsigned int)h;
    }
    *out = cp;
    return 0;
}

static char *parse_string(parser *p) {
    char *buf = NULL;
    size_t len = 0;
    size_t cap = 0;
    if (p->p >= p->end || *p->p != '"') {
        set_err(p, "expected string");
        return NULL;
    }
    p->p++;
    while (p->p < p->end) {
        unsigned char c = (unsigned char)*p->p++;
        if (c == '"') {
            if (!buf) {
                buf = calloc(1, 1);
            }
            return buf;
        }
        if (c == '\\') {
            unsigned char e;
            if (p->p >= p->end) {
                set_err(p, "truncated string escape");
                free(buf);
                return NULL;
            }
            e = (unsigned char)*p->p++;
            if (e == 'u') {
                unsigned int cp = 0;
                if (parse_hex4(p, &cp) != 0) {
                    free(buf);
                    return NULL;
                }
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    unsigned int low = 0;
                    if (p->p + 1 >= p->end || p->p[0] != '\\' || p->p[1] != 'u') {
                        set_err(p, "lone surrogate");
                        free(buf);
                        return NULL;
                    }
                    p->p += 2;
                    if (parse_hex4(p, &low) != 0 || low < 0xDC00 || low > 0xDFFF) {
                        set_err(p, "bad surrogate pair");
                        free(buf);
                        return NULL;
                    }
                    cp = 0x10000 + (((cp - 0xD800) << 10) | (low - 0xDC00));
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    set_err(p, "lone surrogate");
                    free(buf);
                    return NULL;
                }
                if (append_utf8(&buf, &len, &cap, cp) != 0) {
                    set_err(p, "out of memory");
                    free(buf);
                    return NULL;
                }
                continue;
            }
            switch (e) {
            case '"':
            case '\\':
            case '/':
                c = e;
                break;
            case 'b':
                c = '\b';
                break;
            case 'f':
                c = '\f';
                break;
            case 'n':
                c = '\n';
                break;
            case 'r':
                c = '\r';
                break;
            case 't':
                c = '\t';
                break;
            default:
                set_err(p, "bad string escape");
                free(buf);
                return NULL;
            }
        } else if (c < 0x20) {
            set_err(p, "raw control character in string");
            free(buf);
            return NULL;
        }
        if (grow(&buf, &len, &cap, 1) != 0) {
            set_err(p, "out of memory");
            free(buf);
            return NULL;
        }
        buf[len++] = (char)c;
        buf[len] = '\0';
    }
    set_err(p, "unterminated string");
    free(buf);
    return NULL;
}

static json_value *parse_value(parser *p);

static json_value *parse_number(parser *p) {
    const char *start = p->p;
    char tmp[64];
    char *end = NULL;
    size_t n;
    json_value *value;
    double number;
    if (p->p < p->end && *p->p == '-') {
        p->p++;
    }
    if (p->p >= p->end || !isdigit((unsigned char)*p->p)) {
        set_err(p, "bad number");
        return NULL;
    }
    if (*p->p == '0') {
        p->p++;
    } else {
        while (p->p < p->end && isdigit((unsigned char)*p->p)) {
            p->p++;
        }
    }
    if (p->p < p->end && *p->p == '.') {
        p->p++;
        if (p->p >= p->end || !isdigit((unsigned char)*p->p)) {
            set_err(p, "bad number");
            return NULL;
        }
        while (p->p < p->end && isdigit((unsigned char)*p->p)) {
            p->p++;
        }
    }
    if (p->p < p->end && (*p->p == 'e' || *p->p == 'E')) {
        p->p++;
        if (p->p < p->end && (*p->p == '+' || *p->p == '-')) {
            p->p++;
        }
        if (p->p >= p->end || !isdigit((unsigned char)*p->p)) {
            set_err(p, "bad number");
            return NULL;
        }
        while (p->p < p->end && isdigit((unsigned char)*p->p)) {
            p->p++;
        }
    }
    n = (size_t)(p->p - start);
    if (n == 0 || n >= sizeof tmp) {
        set_err(p, "number is too long");
        return NULL;
    }
    memcpy(tmp, start, n);
    tmp[n] = '\0';
    number = strtod(tmp, &end);
    if (!end || *end != '\0') {
        set_err(p, "bad number");
        return NULL;
    }
    value = make_value(JSON_NUMBER);
    if (!value) {
        set_err(p, "out of memory");
        return NULL;
    }
    value->number = number;
    return value;
}

static int match_literal(parser *p, const char *lit) {
    size_t n = strlen(lit);
    if ((size_t)(p->end - p->p) < n || memcmp(p->p, lit, n) != 0) {
        return 0;
    }
    p->p += n;
    return 1;
}

static json_value *parse_array(parser *p) {
    json_value *array = make_value(JSON_ARRAY);
    if (!array) {
        set_err(p, "out of memory");
        return NULL;
    }
    p->p++;
    skip_ws(p);
    if (p->p < p->end && *p->p == ']') {
        p->p++;
        return array;
    }
    for (;;) {
        json_value *item;
        json_value **next;
        skip_ws(p);
        item = parse_value(p);
        if (!item) {
            json_free(array);
            return NULL;
        }
        next = realloc(array->items, (array->count + 1) * sizeof(*next));
        if (!next) {
            json_free(item);
            json_free(array);
            set_err(p, "out of memory");
            return NULL;
        }
        array->items = next;
        array->items[array->count++] = item;
        skip_ws(p);
        if (p->p < p->end && *p->p == ',') {
            p->p++;
            continue;
        }
        if (p->p < p->end && *p->p == ']') {
            p->p++;
            return array;
        }
        set_err(p, "expected ',' or ']'");
        json_free(array);
        return NULL;
    }
}

static json_value *parse_object(parser *p) {
    json_value *object = make_value(JSON_OBJECT);
    if (!object) {
        set_err(p, "out of memory");
        return NULL;
    }
    p->p++;
    skip_ws(p);
    if (p->p < p->end && *p->p == '}') {
        p->p++;
        return object;
    }
    for (;;) {
        char *key;
        json_value *item;
        char **keys;
        json_value **items;
        skip_ws(p);
        key = parse_string(p);
        if (!key) {
            json_free(object);
            return NULL;
        }
        skip_ws(p);
        if (p->p >= p->end || *p->p != ':') {
            free(key);
            json_free(object);
            set_err(p, "expected ':'");
            return NULL;
        }
        p->p++;
        item = parse_value(p);
        if (!item) {
            free(key);
            json_free(object);
            return NULL;
        }
        keys = realloc(object->keys, (object->count + 1) * sizeof(*keys));
        if (!keys) {
            free(key);
            json_free(item);
            json_free(object);
            set_err(p, "out of memory");
            return NULL;
        }
        object->keys = keys;
        items = realloc(object->items, (object->count + 1) * sizeof(*items));
        if (!items) {
            free(key);
            json_free(item);
            json_free(object);
            set_err(p, "out of memory");
            return NULL;
        }
        object->items = items;
        object->keys[object->count] = key;
        object->items[object->count] = item;
        object->count++;
        skip_ws(p);
        if (p->p < p->end && *p->p == ',') {
            p->p++;
            continue;
        }
        if (p->p < p->end && *p->p == '}') {
            p->p++;
            return object;
        }
        set_err(p, "expected ',' or '}'");
        json_free(object);
        return NULL;
    }
}

static json_value *parse_value(parser *p) {
    skip_ws(p);
    if (p->p >= p->end) {
        set_err(p, "unexpected end of JSON");
        return NULL;
    }
    if (*p->p == '"') {
        json_value *value = make_value(JSON_STRING);
        if (!value) {
            set_err(p, "out of memory");
            return NULL;
        }
        value->string = parse_string(p);
        if (!value->string) {
            json_free(value);
            return NULL;
        }
        return value;
    }
    if (*p->p == '{') {
        return parse_object(p);
    }
    if (*p->p == '[') {
        return parse_array(p);
    }
    if (*p->p == 't') {
        json_value *value;
        if (!match_literal(p, "true")) {
            set_err(p, "expected true");
            return NULL;
        }
        value = make_value(JSON_BOOL);
        if (!value) {
            set_err(p, "out of memory");
            return NULL;
        }
        value->boolean = 1;
        return value;
    }
    if (*p->p == 'f') {
        json_value *value;
        if (!match_literal(p, "false")) {
            set_err(p, "expected false");
            return NULL;
        }
        value = make_value(JSON_BOOL);
        if (!value) {
            set_err(p, "out of memory");
            return NULL;
        }
        value->boolean = 0;
        return value;
    }
    if (*p->p == 'n') {
        if (!match_literal(p, "null")) {
            set_err(p, "expected null");
            return NULL;
        }
        return make_value(JSON_NULL);
    }
    if (*p->p == '-' || isdigit((unsigned char)*p->p)) {
        return parse_number(p);
    }
    set_err(p, "unexpected token");
    return NULL;
}

json_value *json_parse(const char *text, char *err, size_t err_len) {
    parser p;
    json_value *value;
    memset(&p, 0, sizeof p);
    if (!text) {
        if (err && err_len) {
            snprintf(err, err_len, "empty input");
        }
        return NULL;
    }
    p.p = text;
    p.end = text + strlen(text);
    value = parse_value(&p);
    if (!value) {
        if (err && err_len) {
            snprintf(err, err_len, "%s", p.err[0] ? p.err : "parse error");
        }
        return NULL;
    }
    skip_ws(&p);
    if (p.p != p.end) {
        json_free(value);
        if (err && err_len) {
            snprintf(err, err_len, "trailing data");
        }
        return NULL;
    }
    if (err && err_len) {
        err[0] = '\0';
    }
    return value;
}

static int append_literal(char **dst, size_t *len, size_t *cap, const char *src) {
    size_t n = strlen(src);
    if (grow(dst, len, cap, n) != 0) {
        return -1;
    }
    memcpy(*dst + *len, src, n);
    *len += n;
    (*dst)[*len] = '\0';
    return 0;
}

int metric_to_json(const np_metric *metric, char *dst, size_t dst_len) {
    char *buf = NULL;
    size_t len = 0;
    size_t cap = 0;
    char num[64];
    int rc = -1;

    if (!metric || !dst || dst_len == 0) {
        return -1;
    }
    if (append_literal(&buf, &len, &cap, "{\"observed_at\":") != 0 ||
        json_append_string(&buf, &len, &cap, metric->observed_at) != 0 ||
        append_literal(&buf, &len, &cap, ",\"device_id\":") != 0) {
        goto done;
    }
    snprintf(num, sizeof num, "%d", metric->device_id);
    if (append_literal(&buf, &len, &cap, num) != 0 ||
        append_literal(&buf, &len, &cap, ",\"device\":") != 0 ||
        json_append_string(&buf, &len, &cap, metric->device_name) != 0 ||
        append_literal(&buf, &len, &cap, ",\"host\":") != 0 ||
        json_append_string(&buf, &len, &cap, metric->host) != 0 ||
        append_literal(&buf, &len, &cap, ",\"check\":") != 0 ||
        json_append_string(&buf, &len, &cap, metric->check) != 0 ||
        append_literal(&buf, &len, &cap, ",\"ok\":") != 0 ||
        append_literal(&buf, &len, &cap, metric->ok ? "true" : "false") != 0) {
        goto done;
    }
    snprintf(num, sizeof num, ",\"latency_ms\":%.3f,\"detail\":", metric->latency_ms);
    if (append_literal(&buf, &len, &cap, num) != 0 ||
        json_append_string(&buf, &len, &cap, metric->detail) != 0 ||
        append_literal(&buf, &len, &cap, "}") != 0) {
        goto done;
    }
    if (len + 1 > dst_len) {
        goto done;
    }
    memcpy(dst, buf, len + 1);
    rc = 0;
done:
    free(buf);
    return rc;
}

void metric_prepare(np_metric *metric, const np_device *device, const char *check) {
    memset(metric, 0, sizeof(*metric));
    metric->device_id = device->id;
    snprintf(metric->device_name, sizeof metric->device_name, "%s", device->name);
    snprintf(metric->host, sizeof metric->host, "%s", device->host);
    snprintf(metric->check, sizeof metric->check, "%s", check);
    np_format_time(metric->observed_at, sizeof metric->observed_at);
}
