import re
from datetime import datetime, timezone

NAME_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]{0,62}$")
HOST_RE = re.compile(r"^[A-Za-z0-9](?:[A-Za-z0-9.-]{0,251}[A-Za-z0-9])?$")
OID_RE = re.compile(r"^[0-9]+(?:\.[0-9]+)*$")
WHEN_RE = re.compile(r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$")
CHECKS = {"icmp", "tcp", "snmp"}


class ValidationError(ValueError):
    pass


def utc_now():
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def _object(data):
    if not isinstance(data, dict):
        raise ValidationError("expected a JSON object")
    return data


def _bool(data, field, default=None):
    if field not in data or data[field] is None:
        if default is None:
            raise ValidationError(f"{field} is required")
        return default
    value = data[field]
    if not isinstance(value, bool):
        raise ValidationError(f"{field} must be a boolean")
    return value


def _port(data, field):
    if field not in data or data[field] is None:
        return None
    value = data[field]
    if isinstance(value, bool) or not isinstance(value, int):
        raise ValidationError(f"{field} must be an integer")
    if value < 1 or value > 65535:
        raise ValidationError(f"{field} must be from 1 to 65535")
    return value


def parse_device(data):
    data = _object(data)
    name = data.get("name")
    host = data.get("host")
    if not isinstance(name, str) or not NAME_RE.match(name):
        raise ValidationError("name must be 1-63 letters, digits, '.', '_' or '-'")
    if not isinstance(host, str) or not HOST_RE.match(host):
        raise ValidationError("host must be an IPv4 address or a hostname")

    community = data.get("snmp_community")
    oid = data.get("snmp_oid")
    if community in (None, "") and oid in (None, ""):
        community = None
        oid = None
        snmp_port = None
    else:
        if not isinstance(community, str) or not 1 <= len(community) <= 64:
            raise ValidationError("snmp_community must be 1-64 characters")
        if any(ord(ch) < 32 or ord(ch) > 126 for ch in community):
            raise ValidationError("snmp_community must be printable ASCII")
        if not isinstance(oid, str) or not OID_RE.match(oid) or len(oid) > 127:
            raise ValidationError("snmp_oid must be a numeric OID")
        snmp_port = _port(data, "snmp_port") or 161

    return {
        "name": name,
        "host": host,
        "icmp_enabled": int(_bool(data, "icmp_enabled", True)),
        "tcp_port": _port(data, "tcp_port"),
        "snmp_port": snmp_port,
        "snmp_community": community,
        "snmp_oid": oid,
        "enabled": int(_bool(data, "enabled", True)),
    }


def parse_metric(data):
    data = _object(data)
    device_id = data.get("device_id")
    if isinstance(device_id, bool) or not isinstance(device_id, int) or device_id <= 0:
        raise ValidationError("device_id must be a positive integer")
    check = data.get("check")
    if check not in CHECKS:
        raise ValidationError("check must be icmp, tcp, or snmp")
    if "latency_ms" not in data:
        raise ValidationError("latency_ms is required")
    latency = data["latency_ms"]
    if isinstance(latency, bool) or not isinstance(latency, (int, float)):
        raise ValidationError("latency_ms must be a number")
    if latency < 0 or latency > 1000000:
        raise ValidationError("latency_ms is out of range")
    detail = data.get("detail", "")
    if detail is None:
        detail = ""
    if not isinstance(detail, str) or len(detail) > 500:
        raise ValidationError("detail must be a string of at most 500 characters")
    observed = data.get("observed_at")
    if observed is None:
        observed = utc_now()
    elif not isinstance(observed, str) or not WHEN_RE.match(observed):
        raise ValidationError("observed_at must look like 2026-11-25T00:00:00Z")
    return {
        "device_id": device_id,
        "check_type": check,
        "ok": int(_bool(data, "ok")),
        "latency_ms": float(latency),
        "detail": detail,
        "observed_at": observed,
    }
