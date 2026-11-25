#ifndef NETPULSE_TEST_HARNESS_H
#define NETPULSE_TEST_HARNESS_H

#include <stdio.h>

extern int np_passed;
extern int np_failed;
extern int np_skipped;

#define EXPECT(cond)                                                         \
    do {                                                                     \
        if (cond) {                                                          \
            np_passed++;                                                     \
        } else {                                                             \
            np_failed++;                                                     \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                    \
    } while (0)

#define SKIP(msg)                                \
    do {                                         \
        np_skipped++;                            \
        fprintf(stderr, "SKIP %s\n", (msg));     \
    } while (0)

#endif
