import os
import sqlite3

from flask import current_app, g

SCHEMA = """
CREATE TABLE IF NOT EXISTS devices (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL UNIQUE,
    host TEXT NOT NULL,
    icmp_enabled INTEGER NOT NULL DEFAULT 1,
    tcp_port INTEGER,
    snmp_port INTEGER,
    snmp_community TEXT,
    snmp_oid TEXT,
    enabled INTEGER NOT NULL DEFAULT 1,
    created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now'))
);

CREATE TABLE IF NOT EXISTS metrics (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    device_id INTEGER NOT NULL REFERENCES devices(id) ON DELETE CASCADE,
    check_type TEXT NOT NULL,
    ok INTEGER NOT NULL,
    latency_ms REAL NOT NULL,
    detail TEXT NOT NULL DEFAULT '',
    observed_at TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_metrics_device_time
    ON metrics(device_id, observed_at);
"""


def connect(path):
    conn = sqlite3.connect(path)
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA foreign_keys = ON")
    conn.execute("PRAGMA journal_mode = WAL")
    return conn


def init_db(path, seed):
    conn = connect(path)
    conn.executescript(SCHEMA)
    if seed and conn.execute("SELECT COUNT(*) AS n FROM devices").fetchone()["n"] == 0:
        _seed(conn)
    conn.commit()
    conn.close()


def _seed(conn):
    tcp_host = os.environ.get("NETPULSE_SEED_TCP_HOST", "127.0.0.1")
    tcp_port = int(os.environ.get("NETPULSE_SEED_TCP_PORT", "5000"))
    snmp_host = os.environ.get("NETPULSE_SEED_SNMP_HOST", "127.0.0.1")
    snmp_port = int(os.environ.get("NETPULSE_SEED_SNMP_PORT", "1161"))
    conn.execute(
        """
        INSERT INTO devices
            (name, host, icmp_enabled, tcp_port, snmp_community, snmp_oid, snmp_port, enabled)
        VALUES ('registry', ?, 1, ?, NULL, NULL, NULL, 1)
        """,
        (tcp_host, tcp_port),
    )
    conn.execute(
        """
        INSERT INTO devices
            (name, host, icmp_enabled, tcp_port, snmp_community, snmp_oid, snmp_port, enabled)
        VALUES ('snmp-agent', ?, 1, NULL, 'public', '1.3.6.1.2.1.1.1.0', ?, 1)
        """,
        (snmp_host, snmp_port),
    )


def get_db():
    if "db" not in g:
        g.db = connect(current_app.config["DB_PATH"])
    return g.db


def close_db(_err=None):
    conn = g.pop("db", None)
    if conn is not None:
        conn.close()
