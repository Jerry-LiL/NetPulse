#include "checks.h"
#include "harness.h"

#include <stdio.h>

int np_passed = 0;
int np_failed = 0;
int np_skipped = 0;

int test_json(void);
int test_devices(void);
int test_config(void);
int test_queue(void);
int test_checks(void);
int test_http(void);
int test_health(void);
int test_poller(void);

int main(void) {
    test_json();
    test_devices();
    test_config();
    test_queue();
    test_checks();
    test_http();
    test_health();
    test_poller();
    snmp_client_shutdown();
    printf("%d passed, %d failed, %d skipped\n", np_passed, np_failed, np_skipped);
    return np_failed == 0 ? 0 : 1;
}
