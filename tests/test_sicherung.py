"""Sicherung -> Einspielen: alles kommt zurueck, Belegung und Einstellungen je Remote, gemeinsame Bereiche."""
from __future__ import annotations

import copy


async def _ws(client, **msg):
    _ws.n = getattr(_ws, "n", 0) + 1
    await client.send_json({"id": _ws.n, **msg})
    r = await client.receive_json()
    assert r["success"], r
    return r["result"]


async def test_sicherung_rundlauf(hass, eingerichtet, hass_ws_client):
    ws = await hass_ws_client(hass)
    kd = hass.data["esphomeremote"]

    # Eigenen Stand herstellen: Belegung geaendert, Einstellung gesetzt, gemeinsamer Bereich belegt
    cfg = copy.deepcopy(kd.entry("remote-wz-test")["config"])
    cfg["activities"][0]["name"] = "Beamer"
    cfg["slots"][2] = "Soundbar"
    await _ws(ws, type="esphomeremote/save", device="remote-wz-test", config=cfg, enabled=True)
    await _ws(ws, type="esphomeremote/einst_set", device="remote-wz-test", werte={"linkshaender": True})
    kd.data["reihenfolge"] = ["remote-wz-zwei", "remote-wz-test"]
    await kd.store.async_save(kd.data)

    sich = await _ws(ws, type="esphomeremote/sicherung")
    assert set(sich["remotes"]) == {"remote-wz-test", "remote-wz-zwei"}
    r = sich["remotes"]["remote-wz-test"]
    assert r["config"]["activities"][0]["name"] == "Beamer" and r["config"]["slots"][2] == "Soundbar"
    assert r["einst"].get("linkshaender") is True
    assert sich["global"]["reihenfolge"] == ["remote-wz-zwei", "remote-wz-test"]
    assert sich["order"][0] == "remote-wz-zwei"

    # Alles kaputt machen ...
    std = copy.deepcopy(kd.entry("remote-wz-zwei")["config"])
    await _ws(ws, type="esphomeremote/save", device="remote-wz-test", config=std, enabled=False)
    await _ws(ws, type="esphomeremote/einst_set", device="remote-wz-test", werte={"linkshaender": False})
    kd.data["reihenfolge"] = []
    assert kd.entry("remote-wz-test")["config"]["activities"][0]["name"] != "Beamer"

    # ... und aus der Sicherung zurueckholen
    await _ws(ws, type="esphomeremote/einspielen", device="remote-wz-test", config=r["config"], enabled=r["enabled"], einst=r["einst"])
    await _ws(ws, type="esphomeremote/sicherung_global", werte=sich["global"])

    e = kd.entry("remote-wz-test")
    assert e["config"]["activities"][0]["name"] == "Beamer" and e["config"]["slots"][2] == "Soundbar"
    assert e["enabled"] is True
    assert kd.einst_werte("remote-wz-test")[0].get("linkshaender") is True
    assert kd.data["reihenfolge"] == ["remote-wz-zwei", "remote-wz-test"]
    # Die andere Remote blieb unberuehrt
    assert kd.entry("remote-wz-zwei")["config"]["activities"][0]["name"] != "Beamer"
