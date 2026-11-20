"""Die Integration startet ohne Supervisor und ohne ESPHome-Ordner (HA im Container) und raeumt sauber auf."""
from __future__ import annotations

DOMAIN = "esphomeremote"


async def test_start_ohne_addon(hass, eingerichtet):
    kd = hass.data[DOMAIN]
    assert kd.devices == ["remote-wz-test", "remote-wz-zwei"]
    for dev in kd.devices:
        e = kd.entry(dev)
        assert e["enabled"] is True
        assert e["config"]["activities"], "Standard-Belegung fehlt"
        assert e["config"]["menu"]["pages"], "Standard-Menue fehlt"


async def test_panel_version_haengt_am_inhalt(integration_pfad):
    """2026-11-10: die feste Kennung "206" liess Browser und App die alte Fassung behalten."""
    from custom_components.esphomeremote import PANEL_VERSION, _panel_version
    import hashlib

    h = hashlib.sha1()
    for name in ("configurator.js", "styles.js", "i18n/en.json", "durchsage-card.js"):
        h.update((integration_pfad / "frontend" / name).read_bytes())
    assert PANEL_VERSION == h.hexdigest()[:8] == _panel_version()
    assert PANEL_VERSION != "206"


async def test_ws_get_liefert_alle_remotes(hass, eingerichtet, hass_ws_client):
    ws = await hass_ws_client(hass)
    await ws.send_json({"id": 1, "type": "esphomeremote/get"})
    r = await ws.receive_json()
    assert r["success"], r
    assert set(r["result"]["devices"]) >= {"remote-wz-test", "remote-wz-zwei"}
    assert r["result"]["order"]
    for d in r["result"]["devices"].values():
        assert "hash" in d and "config" in d and "einst" in d


async def test_durchsage_web_zu_kurz_und_ohne_login(hass, eingerichtet, hass_client, hass_client_no_auth):
    """Durchsage-Karte (2026-11-19): ohne Login 401, eine zu kurze Aufnahme gibt die Meldung der Remote zurueck."""
    anon = await hass_client_no_auth()
    r = await anon.post("/api/esphomeremote/durchsage_web?rate=16000", data=b"\0" * 3200)
    assert r.status == 401
    c = await hass_client()
    r = await c.post("/api/esphomeremote/durchsage_web?rate=16000", data=b"\0" * 3200)
    j = await r.json()
    assert r.status == 200 and j["ok"] is False and j["text"]
    r = await c.post("/api/esphomeremote/durchsage_web?rate=44100", data=b"\0" * 3200)
    assert r.status == 400
