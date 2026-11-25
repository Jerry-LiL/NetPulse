FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends snmpd \
    && rm -rf /var/lib/apt/lists/*

COPY docker/snmpd.conf /etc/snmp/netpulse.conf
EXPOSE 161/udp
CMD ["snmpd", "-f", "-Lo", "-C", "-c", "/etc/snmp/netpulse.conf"]
