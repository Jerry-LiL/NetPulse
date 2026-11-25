#!/bin/sh
# Foreground snmpd on 127.0.0.1:1161 for local SNMP checks.
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
dir=${TMPDIR:-/tmp}/netpulse-snmpd
mkdir -p "$dir"
export SNMP_PERSISTENT_DIR="$dir"
export SNMPCONFPATH="$dir"
exec snmpd -f -Lo -C -c "$root/docker/snmpd.local.conf" -p "$dir/snmpd.pid"
