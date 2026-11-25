#include "devices.h"
#include "harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int test_devices(void) {
    char path[] = "/tmp/netpulse-devices-XXXXXX";
    int fd;
    FILE *file;
    np_device *devices = NULL;
    size_t count = 0;
    char err[256];
    const char *json =
        "{\"devices\":[{\"id\":4,\"name\":\"sw1\",\"host\":\"192.0.2.10\","
        "\"icmp_enabled\":true,\"tcp_port\":22,\"snmp_community\":\"public\","
        "\"snmp_oid\":\"1.3.6.1.2.1.1.1.0\",\"snmp_port\":1161,\"enabled\":true},"
        "{\"id\":5,\"name\":\"web\",\"host\":\"192.0.2.11\",\"icmp_enabled\":false,"
        "\"tcp_port\":null,\"snmp_community\":null,\"snmp_oid\":null}]}";

    fd = mkstemp(path);
    EXPECT(fd >= 0);
    file = fdopen(fd, "w");
    EXPECT(file != NULL);
    fputs("# a comment\n\n", file);
    fputs("1|edge|10.0.0.5|1|22|1161|public|1.3.6.1.2.1.1.1.0\n", file);
    fputs("2|web|127.0.0.1|0|80|0||\n", file);
    fclose(file);

    EXPECT(devices_load_file(path, &devices, &count, err, sizeof err) == 0);
    EXPECT(count == 2);
    EXPECT(devices[0].id == 1);
    EXPECT(strcmp(devices[0].name, "edge") == 0);
    EXPECT(devices[0].icmp_enabled == 1);
    EXPECT(devices[0].tcp_port == 22);
    EXPECT(devices[0].snmp_port == 1161);
    EXPECT(strcmp(devices[0].snmp_oid, "1.3.6.1.2.1.1.1.0") == 0);
    EXPECT(devices[1].icmp_enabled == 0);
    EXPECT(devices[1].tcp_port == 80);
    EXPECT(devices[1].snmp_port == 0);
    EXPECT(devices[1].snmp_community[0] == '\0');
    devices_free(devices);

    file = fopen(path, "w");
    fputs("1|only-three|fields\n", file);
    fclose(file);
    EXPECT(devices_load_file(path, &devices, &count, err, sizeof err) != 0);
    EXPECT(strstr(err, "line 1") != NULL);
    unlink(path);

    EXPECT(devices_from_json(json, &devices, &count, err, sizeof err) == 0);
    EXPECT(count == 2);
    EXPECT(devices[0].id == 4);
    EXPECT(devices[0].icmp_enabled == 1);
    EXPECT(devices[0].tcp_port == 22);
    EXPECT(devices[0].snmp_port == 1161);
    EXPECT(strcmp(devices[0].snmp_community, "public") == 0);
    EXPECT(devices[1].icmp_enabled == 0);
    EXPECT(devices[1].tcp_port == 0);
    EXPECT(devices[1].snmp_oid[0] == '\0');
    devices_free(devices);

    EXPECT(devices_from_json("{\"devices\":[{\"id\":1,\"host\":\"x\"}]}", &devices, &count,
                             err, sizeof err) != 0);
    EXPECT(devices == NULL);
    return 0;
}
