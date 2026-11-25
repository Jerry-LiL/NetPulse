import sqlite3

from flask import Blueprint, jsonify, render_template, request

from netpulse_registry.db import get_db
from netpulse_registry.validate import ValidationError, parse_device, parse_metric

bp = Blueprint("registry", __name__)


def _error(message, status):
    return jsonify(error=message), status


def device_dict(row):
    return {
        "id": row["id"],
        "name": row["name"],
        "host": row["host"],
        "icmp_enabled": bool(row["icmp_enabled"]),
        "tcp_port": row["tcp_port"],
        "snmp_port": row["snmp_port"],
        "snmp_community": row["snmp_community"],
        "snmp_oid": row["snmp_oid"],
        "enabled": bool(row["enabled"]),
        "created_at": row["created_at"],
    }


def metric_dict(row):
    return {
        "id": row["id"],
        "device_id": row["device_id"],
        "device": row["device_name"],
        "check": row["check_type"],
        "ok": bool(row["ok"]),
        "latency_ms": row["latency_ms"],
        "detail": row["detail"],
        "observed_at": row["observed_at"],
    }


def _json_body():
    data = request.get_json(silent=True)
    if data is None:
        raise ValidationError("expected a JSON object")
    return data


@bp.get("/health")
def health():
    get_db().execute("SELECT 1").fetchone()
    return jsonify(status="ok")


@bp.get("/")
def index():
    db = get_db()
    devices = db.execute("SELECT * FROM devices ORDER BY name COLLATE NOCASE").fetchall()
    metrics = db.execute(
        """
        SELECT m.id, m.device_id, d.name AS device_name, m.check_type, m.ok,
               m.latency_ms, m.detail, m.observed_at
        FROM metrics m
        JOIN devices d ON d.id = m.device_id
        ORDER BY m.id DESC
        LIMIT 20
        """
    ).fetchall()
    return render_template("index.html", devices=devices, metrics=metrics)


@bp.get("/api/devices")
def list_devices():
    enabled = request.args.get("enabled")
    sql = "SELECT * FROM devices"
    args = []
    if enabled is not None:
        if enabled not in {"0", "1", "true", "false", "yes", "no"}:
            return _error("enabled must be 0 or 1", 400)
        sql += " WHERE enabled = ?"
        args.append(1 if enabled in {"1", "true", "yes"} else 0)
    sql += " ORDER BY id"
    rows = get_db().execute(sql, args).fetchall()
    return jsonify(devices=[device_dict(row) for row in rows])


@bp.get("/api/devices/<int:device_id>")
def get_device(device_id):
    row = get_db().execute("SELECT * FROM devices WHERE id = ?", (device_id,)).fetchone()
    if row is None:
        return _error("device not found", 404)
    return jsonify(device_dict(row))


@bp.post("/api/devices")
def create_device():
    try:
        fields = parse_device(_json_body())
    except ValidationError as exc:
        return _error(str(exc), 400)
    db = get_db()
    try:
        cur = db.execute(
            """
            INSERT INTO devices
                (name, host, icmp_enabled, tcp_port, snmp_port, snmp_community, snmp_oid, enabled)
            VALUES (:name, :host, :icmp_enabled, :tcp_port, :snmp_port,
                    :snmp_community, :snmp_oid, :enabled)
            """,
            fields,
        )
        db.commit()
    except sqlite3.IntegrityError:
        return _error("a device with that name already exists", 409)
    row = db.execute("SELECT * FROM devices WHERE id = ?", (cur.lastrowid,)).fetchone()
    return jsonify(device_dict(row)), 201


@bp.put("/api/devices/<int:device_id>")
def update_device(device_id):
    db = get_db()
    if db.execute("SELECT id FROM devices WHERE id = ?", (device_id,)).fetchone() is None:
        return _error("device not found", 404)
    try:
        fields = parse_device(_json_body())
    except ValidationError as exc:
        return _error(str(exc), 400)
    fields["id"] = device_id
    try:
        db.execute(
            """
            UPDATE devices
            SET name = :name, host = :host, icmp_enabled = :icmp_enabled,
                tcp_port = :tcp_port, snmp_port = :snmp_port,
                snmp_community = :snmp_community, snmp_oid = :snmp_oid,
                enabled = :enabled
            WHERE id = :id
            """,
            fields,
        )
        db.commit()
    except sqlite3.IntegrityError:
        return _error("a device with that name already exists", 409)
    row = db.execute("SELECT * FROM devices WHERE id = ?", (device_id,)).fetchone()
    return jsonify(device_dict(row))


@bp.delete("/api/devices/<int:device_id>")
def delete_device(device_id):
    db = get_db()
    cur = db.execute("DELETE FROM devices WHERE id = ?", (device_id,))
    db.commit()
    if cur.rowcount == 0:
        return _error("device not found", 404)
    return "", 204


@bp.post("/api/metrics")
def create_metric():
    try:
        fields = parse_metric(_json_body())
    except ValidationError as exc:
        return _error(str(exc), 400)
    db = get_db()
    if db.execute("SELECT id FROM devices WHERE id = ?", (fields["device_id"],)).fetchone() is None:
        return _error("device not found", 404)
    cur = db.execute(
        """
        INSERT INTO metrics (device_id, check_type, ok, latency_ms, detail, observed_at)
        VALUES (:device_id, :check_type, :ok, :latency_ms, :detail, :observed_at)
        """,
        fields,
    )
    db.commit()
    row = db.execute(
        """
        SELECT m.id, m.device_id, d.name AS device_name, m.check_type, m.ok,
               m.latency_ms, m.detail, m.observed_at
        FROM metrics m
        JOIN devices d ON d.id = m.device_id
        WHERE m.id = ?
        """,
        (cur.lastrowid,),
    ).fetchone()
    return jsonify(metric_dict(row)), 201


@bp.get("/api/metrics")
def list_metrics():
    limit_text = request.args.get("limit", "50")
    try:
        limit = int(limit_text)
    except ValueError:
        return _error("limit must be an integer", 400)
    if limit < 1 or limit > 500:
        return _error("limit must be from 1 to 500", 400)

    clauses = []
    args = []
    device_text = request.args.get("device_id")
    if device_text is not None:
        try:
            device_id = int(device_text)
        except ValueError:
            return _error("device_id must be an integer", 400)
        clauses.append("m.device_id = ?")
        args.append(device_id)
    check = request.args.get("check")
    if check is not None:
        if check not in {"icmp", "tcp", "snmp"}:
            return _error("check must be icmp, tcp, or snmp", 400)
        clauses.append("m.check_type = ?")
        args.append(check)
    sql = """
        SELECT m.id, m.device_id, d.name AS device_name, m.check_type, m.ok,
               m.latency_ms, m.detail, m.observed_at
        FROM metrics m
        JOIN devices d ON d.id = m.device_id
    """
    if clauses:
        sql += " WHERE " + " AND ".join(clauses)
    sql += " ORDER BY m.id DESC LIMIT ?"
    args.append(limit)
    rows = get_db().execute(sql, args).fetchall()
    return jsonify(metrics=[metric_dict(row) for row in rows])


@bp.app_errorhandler(404)
def not_found(_err):
    if request.path.startswith("/api"):
        return _error("not found", 404)
    return "not found\n", 404
