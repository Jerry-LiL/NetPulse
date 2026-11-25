#include "signals.h"

#include <signal.h>
#include <string.h>

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int signo) {
    (void)signo;
    g_stop = 1;
}

static void install_one(int signo) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    /* Leave SA_RESTART unset so sleep and poll wake up on Ctrl-C. */
    sa.sa_flags = 0;
    sigaction(signo, &sa, NULL);
}

void signals_install(void) {
    install_one(SIGINT);
    install_one(SIGTERM);
}

int signals_stop_requested(void) {
    return g_stop != 0;
}
