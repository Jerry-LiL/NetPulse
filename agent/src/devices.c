#include "devices.h"
#include "json_util.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_err(char *err, size_t err_len, const char *msg) {
    if (err && err_len) {
        snprintf(err, err_len, "%s", msg);
    }
}

static char *trim(char *s) {
    char *end;
    while (*s && isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return s;
    }
    end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) {
        *end-- = '\0';
    }
    return s;
}

static int copy_field(char *dst, size_t dst_len, const char *src) {
    size_t n = strlen(src);
    if (n >= dst_len) {
        return -1;
    }
    memcpy(dst, src, n + 1);
    return 0;
}

static int parse_port(const char *text, int *out) {
    char *end = NULL;
    long value;
    errno = 0;
    value = strtol(text, &end, 10);
    if (end == text || *end != '\0' || errno != 0 || value < 0 || value > 65535) {
        return -1;
    }
    *out = (int)value;
    return 0;
}

static int split_pipes(char *line, char **fields, int expected) {
    int count = 0;
    char *cursor = line;
    while (count < expected) {
        char *pipe = strchr(cursor, '|');
        fields[count++] = cursor;
        if (count == expected) {
            if (pipe) {
                return -1;
            }
            return 0;
        }
        if (!pipe) {
            return -1;
        }
        *pipe = '\0';
        cursor = pipe + 1;
    }
    return -1;
}

static int parse_device_fields(char **fields, np_device *device, char *err, size_t err_len) {
    char *end = NULL;
    long id;
    int icmp = 0;
    int tcp_port = 0;
    int snmp_port = 0;
    memset(device, 0, sizeof(*device));
    errno = 0;
    id = strtol(trim(fields[0]), &end, 10);
    if (end == trim(fields[0]) || *end != '\0' || errno != 0 || id <= 0 || id > 1000000000L) {
        set_err(err, err_len, "device id must be a positive integer");
        return -1;
    }
    device->id = (int)id;
    if (copy_field(device->name, sizeof device->name, trim(fields[1])) != 0 ||
        device->name[0] == '\0') {
        set_err(err, err_len, "device name is missing or too long");
        return -1;
    }
    if (copy_field(device->host, sizeof device->host, trim(fields[2])) != 0 ||
        device->host[0] == '\0') {
        set_err(err, err_len, "device host is missing or too long");
        return -1;
    }
    if (strcmp(trim(fields[3]), "1") == 0) {
        icmp = 1;
    } else if (strcmp(trim(fields[3]), "0") != 0) {
        set_err(err, err_len, "icmp flag must be 0 or 1");
        return -1;
    }
    if (parse_port(trim(fields[4]), &tcp_port) != 0 || parse_port(trim(fields[5]), &snmp_port) != 0) {
        set_err(err, err_len, "ports must be integers from 0 to 65535");
        return -1;
    }
    if (copy_field(device->snmp_community, sizeof device->snmp_community, trim(fields[6])) != 0 ||
        copy_field(device->snmp_oid, sizeof device->snmp_oid, trim(fields[7])) != 0) {
        set_err(err, err_len, "snmp community or oid is too long");
        return -1;
    }
    device->icmp_enabled = icmp;
    device->tcp_port = tcp_port;
    if (device->snmp_community[0] == '\0' || device->snmp_oid[0] == '\0') {
        device->snmp_community[0] = '\0';
        device->snmp_oid[0] = '\0';
        device->snmp_port = 0;
    } else {
        device->snmp_port = snmp_port > 0 ? snmp_port : 161;
    }
    return 0;
}

int devices_load_file(const char *path, np_device **out, size_t *count,
                      char *err, size_t err_len) {
    FILE *file;
    char line[1024];
    np_device *list = NULL;
    size_t n = 0;
    size_t cap = 0;
    int line_no = 0;

    *out = NULL;
    *count = 0;
    file = fopen(path, "r");
    if (!file) {
        snprintf(err, err_len, "open %s: %s", path, strerror(errno));
        return -1;
    }
    while (fgets(line, sizeof line, file)) {
        char *fields[8];
        char *start;
        np_device device;
        line_no++;
        if (strchr(line, '\n') == NULL && !feof(file)) {
            snprintf(err, err_len, "line %d is longer than %zu bytes", line_no, sizeof line - 1);
            free(list);
            fclose(file);
            return -1;
        }
        start = trim(line);
        if (start[0] == '\0' || start[0] == '#') {
            continue;
        }
        if (split_pipes(start, fields, 8) != 0) {
            snprintf(err, err_len, "line %d must have 8 pipe-separated fields", line_no);
            free(list);
            fclose(file);
            return -1;
        }
        if (parse_device_fields(fields, &device, err, err_len) != 0) {
            char detail[256];
            snprintf(detail, sizeof detail, "line %d: %s", line_no, err[0] ? err : "bad device");
            snprintf(err, err_len, "%s", detail);
            free(list);
            fclose(file);
            return -1;
        }
        if (n == cap) {
            size_t next_cap = cap ? cap * 2 : 8;
            np_device *grown = realloc(list, next_cap * sizeof(*grown));
            if (!grown) {
                set_err(err, err_len, "out of memory");
                free(list);
                fclose(file);
                return -1;
            }
            list = grown;
            cap = next_cap;
        }
        list[n++] = device;
    }
    fclose(file);
    *out = list;
    *count = n;
    if (err && err_len) {
        err[0] = '\0';
    }
    return 0;
}

static int json_int_field(const json_value *object, const char *key, int required,
                          int *present, int *out, char *err, size_t err_len) {
    const json_value *value = json_object_get(object, key);
    double number;
    int as_int;
    *present = 0;
    if (!value || value->type == JSON_NULL) {
        if (required) {
            snprintf(err, err_len, "%s is required", key);
            return -1;
        }
        return 0;
    }
    if (value->type != JSON_NUMBER) {
        snprintf(err, err_len, "%s must be a number", key);
        return -1;
    }
    number = value->number;
    if (number < -2147483648.0 || number > 2147483647.0) {
        snprintf(err, err_len, "%s is out of range", key);
        return -1;
    }
    as_int = (int)number;
    if ((double)as_int != number) {
        snprintf(err, err_len, "%s must be an integer", key);
        return -1;
    }
    *present = 1;
    *out = as_int;
    return 0;
}

static int json_string_field(const json_value *object, const char *key, int required,
                             char *dst, size_t dst_len, char *err, size_t err_len) {
    const json_value *value = json_object_get(object, key);
    dst[0] = '\0';
    if (!value || value->type == JSON_NULL) {
        if (required) {
            snprintf(err, err_len, "%s is required", key);
            return -1;
        }
        return 0;
    }
    if (value->type != JSON_STRING) {
        snprintf(err, err_len, "%s must be a string", key);
        return -1;
    }
    if (strlen(value->string) >= dst_len) {
        snprintf(err, err_len, "%s is too long", key);
        return -1;
    }
    memcpy(dst, value->string, strlen(value->string) + 1);
    return 0;
}

static int json_bool_field(const json_value *object, const char *key, int fallback,
                           int *out, char *err, size_t err_len) {
    const json_value *value = json_object_get(object, key);
    if (!value || value->type == JSON_NULL) {
        *out = fallback;
        return 0;
    }
    if (value->type != JSON_BOOL) {
        snprintf(err, err_len, "%s must be a boolean", key);
        return -1;
    }
    *out = value->boolean ? 1 : 0;
    return 0;
}

int devices_from_json(const char *text, np_device **out, size_t *count,
                      char *err, size_t err_len) {
    json_value *root;
    const json_value *devices;
    np_device *list = NULL;
    size_t i;
    size_t n = 0;

    *out = NULL;
    *count = 0;
    root = json_parse(text, err, err_len);
    if (!root) {
        return -1;
    }
    if (root->type != JSON_OBJECT) {
        set_err(err, err_len, "registry payload must be an object");
        json_free(root);
        return -1;
    }
    devices = json_object_get(root, "devices");
    if (!devices || devices->type != JSON_ARRAY) {
        set_err(err, err_len, "registry payload needs a devices array");
        json_free(root);
        return -1;
    }
    if (devices->count > 0) {
        list = calloc(devices->count, sizeof(*list));
        if (!list) {
            set_err(err, err_len, "out of memory");
            json_free(root);
            return -1;
        }
    }
    for (i = 0; i < devices->count; i++) {
        const json_value *item = devices->items[i];
        np_device *device = &list[i];
        int present = 0;
        int id = 0;
        int tcp_port = 0;
        int snmp_port = 0;
        if (!item || item->type != JSON_OBJECT) {
            set_err(err, err_len, "device entry must be an object");
            free(list);
            json_free(root);
            return -1;
        }
        if (json_int_field(item, "id", 1, &present, &id, err, err_len) != 0 ||
            json_string_field(item, "name", 1, device->name, sizeof device->name, err, err_len) != 0 ||
            json_string_field(item, "host", 1, device->host, sizeof device->host, err, err_len) != 0 ||
            json_bool_field(item, "icmp_enabled", 0, &device->icmp_enabled, err, err_len) != 0 ||
            json_int_field(item, "tcp_port", 0, &present, &tcp_port, err, err_len) != 0 ||
            json_string_field(item, "snmp_community", 0, device->snmp_community,
                              sizeof device->snmp_community, err, err_len) != 0 ||
            json_string_field(item, "snmp_oid", 0, device->snmp_oid, sizeof device->snmp_oid,
                              err, err_len) != 0) {
            free(list);
            json_free(root);
            return -1;
        }
        present = 0;
        if (json_int_field(item, "snmp_port", 0, &present, &snmp_port, err, err_len) != 0) {
            free(list);
            json_free(root);
            return -1;
        }
        if (id <= 0 || device->name[0] == '\0' || device->host[0] == '\0') {
            set_err(err, err_len, "device id, name, and host are required");
            free(list);
            json_free(root);
            return -1;
        }
        if (tcp_port < 0 || tcp_port > 65535 || snmp_port < 0 || snmp_port > 65535) {
            set_err(err, err_len, "ports must be from 0 to 65535");
            free(list);
            json_free(root);
            return -1;
        }
        device->id = id;
        device->tcp_port = tcp_port;
        if (device->snmp_community[0] == '\0' || device->snmp_oid[0] == '\0') {
            device->snmp_community[0] = '\0';
            device->snmp_oid[0] = '\0';
            device->snmp_port = 0;
        } else {
            device->snmp_port = snmp_port > 0 ? snmp_port : 161;
        }
    }
    n = devices->count;
    json_free(root);
    *out = list;
    *count = n;
    if (err && err_len) {
        err[0] = '\0';
    }
    return 0;
}

void devices_free(np_device *devices) {
    free(devices);
}
