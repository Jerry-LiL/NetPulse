import pytest

from netpulse_registry import create_app


@pytest.fixture
def client(tmp_path):
    app = create_app(db_path=str(tmp_path / "registry.db"), seed=False)
    app.config["TESTING"] = True
    with app.test_client() as test_client:
        yield test_client


def device_payload(**overrides):
    body = {
        "name": "edge-1",
        "host": "192.0.2.10",
        "icmp_enabled": True,
        "tcp_port": 22,
        "snmp_community": "public",
        "snmp_oid": "1.3.6.1.2.1.1.1.0",
        "snmp_port": 161,
        "enabled": True,
    }
    body.update(overrides)
    return body


def test_health(client):
    response = client.get("/health")
    assert response.status_code == 200
    assert response.get_json()["status"] == "ok"


def test_device_lifecycle(client):
    created = client.post("/api/devices", json=device_payload())
    assert created.status_code == 201
    device = created.get_json()
    assert device["id"] > 0
    assert device["icmp_enabled"] is True
    assert device["snmp_oid"] == "1.3.6.1.2.1.1.1.0"

    listed = client.get("/api/devices?enabled=1")
    assert listed.status_code == 200
    body = listed.get_json()
    assert len(body["devices"]) == 1
    keys = set(body["devices"][0])
    assert {"id", "name", "host", "icmp_enabled", "tcp_port", "snmp_community",
            "snmp_oid", "snmp_port", "enabled"} <= keys

    fetched = client.get(f"/api/devices/{device['id']}")
    assert fetched.status_code == 200
    assert fetched.get_json()["name"] == "edge-1"

    updated = client.put(
        f"/api/devices/{device['id']}",
        json=device_payload(name="edge-2", icmp_enabled=False, tcp_port=None,
                            snmp_community=None, snmp_oid=None, enabled=False),
    )
    assert updated.status_code == 200
    assert updated.get_json()["name"] == "edge-2"
    assert updated.get_json()["tcp_port"] is None
    assert updated.get_json()["snmp_community"] is None
    assert updated.get_json()["enabled"] is False

    hidden = client.get("/api/devices?enabled=1")
    assert hidden.get_json()["devices"] == []
    visible = client.get("/api/devices?enabled=0")
    assert len(visible.get_json()["devices"]) == 1

    deleted = client.delete(f"/api/devices/{device['id']}")
    assert deleted.status_code == 204
    assert client.get(f"/api/devices/{device['id']}").status_code == 404


def test_validation_and_conflicts(client):
    assert client.post("/api/devices", json={"host": "192.0.2.10"}).status_code == 400
    assert client.post("/api/devices", json=device_payload(tcp_port=70000)).status_code == 400
    assert client.post("/api/devices", json=device_payload(snmp_oid=None)).status_code == 400
    assert client.post("/api/devices", json=device_payload(host="http://bad")).status_code == 400
    assert client.post("/api/devices", data="not-json", content_type="text/plain").status_code == 400

    assert client.post("/api/devices", json=device_payload()).status_code == 201
    conflict = client.post("/api/devices", json=device_payload(host="192.0.2.11"))
    assert conflict.status_code == 409
    assert client.get("/api/devices?enabled=maybe").status_code == 400


def test_metrics_round_trip_and_cascade(client):
    device = client.post("/api/devices", json=device_payload(name="core")).get_json()
    missing = client.post("/api/metrics", json={
        "device_id": 9999,
        "check": "tcp",
        "ok": False,
        "latency_ms": 1,
        "detail": "nope",
    })
    assert missing.status_code == 404

    posted = client.post("/api/metrics", json={
        "device_id": device["id"],
        "check": "tcp",
        "ok": True,
        "latency_ms": 1.5,
        "detail": "connected <b>ok</b>",
        "observed_at": "2026-11-25T12:00:00Z",
    })
    assert posted.status_code == 201
    stored = posted.get_json()
    assert stored["check"] == "tcp"
    assert stored["ok"] is True
    assert stored["device"] == "core"
    assert stored["observed_at"] == "2026-11-25T12:00:00Z"

    client.post("/api/metrics", json={
        "device_id": device["id"],
        "check": "icmp",
        "ok": False,
        "latency_ms": 12,
        "detail": "timeout",
        "observed_at": "2026-11-25T12:00:01Z",
    })
    tcp_only = client.get("/api/metrics", query_string={"device_id": device["id"], "check": "tcp"})
    assert tcp_only.status_code == 200
    metrics = tcp_only.get_json()["metrics"]
    assert len(metrics) == 1
    assert metrics[0]["check"] == "tcp"

    bad_time = client.post("/api/metrics", json={
        "device_id": device["id"],
        "check": "snmp",
        "ok": True,
        "latency_ms": 1,
        "observed_at": "yesterday",
    })
    assert bad_time.status_code == 400

    page = client.get("/")
    assert page.status_code == 200
    html = page.get_data(as_text=True)
    assert "core" in html
    assert "connected &lt;b&gt;ok&lt;/b&gt;" in html
    assert "<b>ok</b>" not in html

    assert client.delete(f"/api/devices/{device['id']}").status_code == 204
    remaining = client.get("/api/metrics").get_json()["metrics"]
    assert remaining == []


def test_unknown_api_path_is_json(client):
    response = client.get("/api/does-not-exist")
    assert response.status_code == 404
    assert response.get_json()["error"] == "not found"


def test_seed_inserts_demo_devices(tmp_path, monkeypatch):
    monkeypatch.setenv("NETPULSE_SEED_TCP_HOST", "registry")
    monkeypatch.setenv("NETPULSE_SEED_TCP_PORT", "5000")
    monkeypatch.setenv("NETPULSE_SEED_SNMP_HOST", "snmpd")
    monkeypatch.setenv("NETPULSE_SEED_SNMP_PORT", "161")
    app = create_app(db_path=str(tmp_path / "seed.db"), seed=True)
    with app.test_client() as test_client:
        devices = test_client.get("/api/devices").get_json()["devices"]
    names = {item["name"]: item for item in devices}
    assert names["registry"]["host"] == "registry"
    assert names["registry"]["tcp_port"] == 5000
    assert names["registry"]["snmp_oid"] is None
    assert names["snmp-agent"]["host"] == "snmpd"
    assert names["snmp-agent"]["snmp_port"] == 161
    assert names["snmp-agent"]["snmp_community"] == "public"
    assert names["snmp-agent"]["icmp_enabled"] is True
