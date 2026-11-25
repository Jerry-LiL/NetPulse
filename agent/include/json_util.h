#ifndef NETPULSE_JSON_UTIL_H
#define NETPULSE_JSON_UTIL_H

#include "netpulse.h"

#include <stddef.h>

typedef enum {
    JSON_NULL = 0,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} json_type;

typedef struct json_value json_value;

struct json_value {
    json_type type;
    int boolean;
    double number;
    char *string;
    json_value **items;
    char **keys;
    size_t count;
};

json_value *json_parse(const char *text, char *err, size_t err_len);
void json_free(json_value *value);
const json_value *json_object_get(const json_value *object, const char *key);

/* Append src as a JSON string literal, including quotes. Returns 0 or -1. */
int json_append_string(char **dst, size_t *len, size_t *cap, const char *src);

int metric_to_json(const np_metric *metric, char *dst, size_t dst_len);
void metric_prepare(np_metric *metric, const np_device *device, const char *check);

#endif
