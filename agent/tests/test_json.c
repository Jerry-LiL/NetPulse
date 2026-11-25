#include "harness.h"
#include "json_util.h"

#include <stdio.h>
#include <string.h>

static int near(double a, double b) {
    double delta = a - b;
    if (delta < 0) {
        delta = -delta;
    }
    return delta < 0.001;
}

int test_json(void) {
    char err[128];
    json_value *root;
    const json_value *item;
    np_metric metric;
    char encoded[1024];
    json_value *again;

    root = json_parse("{\"name\":\"edge\",\"ok\":true,\"down\":false,\"port\":null,\"n\":12.5,"
                      "\"msg\":\"a\\\"b\\\\c\\n\",\"ch\":\"\\u0041\",\"emoji\":\"\\uD83D\\uDE00\","
                      "\"tags\":[\"a\",2]}",
                      err, sizeof err);
    EXPECT(root != NULL);
    EXPECT(root && root->type == JSON_OBJECT);
    item = json_object_get(root, "name");
    EXPECT(item && item->type == JSON_STRING && strcmp(item->string, "edge") == 0);
    item = json_object_get(root, "ok");
    EXPECT(item && item->type == JSON_BOOL && item->boolean == 1);
    item = json_object_get(root, "down");
    EXPECT(item && item->type == JSON_BOOL && item->boolean == 0);
    item = json_object_get(root, "port");
    EXPECT(item && item->type == JSON_NULL);
    item = json_object_get(root, "n");
    EXPECT(item && item->type == JSON_NUMBER && item->number == 12.5);
    item = json_object_get(root, "msg");
    EXPECT(item && item->type == JSON_STRING && strcmp(item->string, "a\"b\\c\n") == 0);
    item = json_object_get(root, "ch");
    EXPECT(item && item->type == JSON_STRING && strcmp(item->string, "A") == 0);
    item = json_object_get(root, "emoji");
    EXPECT(item && item->type == JSON_STRING && strcmp(item->string, "\xF0\x9F\x98\x80") == 0);
    item = json_object_get(root, "tags");
    EXPECT(item && item->type == JSON_ARRAY && item->count == 2);
    EXPECT(item && strcmp(item->items[0]->string, "a") == 0);
    EXPECT(item && item->items[1]->number == 2);
    json_free(root);

    EXPECT(json_parse("{\"a\":1} trailing", err, sizeof err) == NULL);
    EXPECT(strstr(err, "trailing") != NULL);
    EXPECT(json_parse("{\"a\":", err, sizeof err) == NULL);
    EXPECT(json_parse("[1, 2,]", err, sizeof err) == NULL);
    EXPECT(json_parse("\"unterminated", err, sizeof err) == NULL);
    root = json_parse("[]", err, sizeof err);
    EXPECT(root && root->type == JSON_ARRAY && root->count == 0);
    json_free(root);

    memset(&metric, 0, sizeof metric);
    metric.device_id = 7;
    snprintf(metric.device_name, sizeof metric.device_name, "core \"1\"");
    snprintf(metric.host, sizeof metric.host, "10.0.0.8");
    snprintf(metric.check, sizeof metric.check, "tcp");
    metric.ok = 1;
    metric.latency_ms = 1.25;
    snprintf(metric.detail, sizeof metric.detail, "connected");
    snprintf(metric.observed_at, sizeof metric.observed_at, "2026-11-25T00:00:00Z");
    EXPECT(metric_to_json(&metric, encoded, sizeof encoded) == 0);
    again = json_parse(encoded, err, sizeof err);
    EXPECT(again != NULL);
    item = json_object_get(again, "device_id");
    EXPECT(item && item->number == 7);
    item = json_object_get(again, "device");
    EXPECT(item && strcmp(item->string, "core \"1\"") == 0);
    item = json_object_get(again, "ok");
    EXPECT(item && item->type == JSON_BOOL && item->boolean == 1);
    item = json_object_get(again, "latency_ms");
    EXPECT(item && near(item->number, 1.25));
    item = json_object_get(again, "check");
    EXPECT(item && strcmp(item->string, "tcp") == 0);
    json_free(again);
    return 0;
}
