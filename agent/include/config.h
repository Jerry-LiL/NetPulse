#ifndef NETPULSE_CONFIG_H
#define NETPULSE_CONFIG_H

#include "netpulse.h"

/* Read process environment into cfg. Returns 0, or -1 on a bad value. */
int config_load(np_config *cfg);

#endif
