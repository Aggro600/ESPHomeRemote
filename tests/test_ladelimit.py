"""Ladelimit: Kontakte aus ab Limit, wieder an unter Limit - 5, bei Entnahme oder nach 2 h; nie ohne Schalter."""
from __future__ import annotations

import time

DOMAIN = "esphomeremote"


class _Dock:
    """Nachgebauter Dock-Stand (dockstatus.DockStatus.stand) - nur das Feld, das das Ladelimit liest."""

    def __init__(self) -> None:
        self.stand: dict = {}


def _schalter(hass):
    rufe = []

    async def h(call):
        rufe.append((call.service, call.data["entity_id"]))
        hass.states.async_set(call.data["entity_id"], "on" if call.service == "turn_on" else "off")

    hass.services.async_register("switch", "turn_on", h)
    hass.services.async_register("switch", "turn_off", h)
    return rufe


async def test_ladelimit_ablauf(hass, eingerichtet):
    ll = hass.data[DOMAIN + "_ladelimit"]
    dock = _Dock()
    hass.data[DOMAIN + "_dockstatus"] = dock
    rufe = _schalter(hass)
    hass.states.async_set("switch.dock_wz_ladekontakte", "on")
    ll.kd.data["ladelimit"] = {"remote-wz-test": 80}

    def meldung(akku, kabel="1", d="dock_wz", alter=0):
        dock.stand["remote-wz-test"] = {"werte": {"a": str(akku), "l": kabel}, "zeit": time.time() - alter, "dock": d}

    meldung(70)
    await ll.pruefen()
    assert rufe == [], "unter dem Limit passiert nichts"

    meldung(80)
    await ll.pruefen()
    assert rufe == [("turn_off", "switch.dock_wz_ladekontakte")]
    assert ll.aus["dock_wz"]["remote"] == "remote-wz-test"

    meldung(78, kabel="0")   # Kontakte aus -> Remote meldet "kein Kabel", noch ueber dem Wiedereinschaltpunkt
    await ll.pruefen()
    assert len(rufe) == 1, "zwischen Limit-5 und Limit bleibt es aus"

    meldung(75, kabel="0")
    await ll.pruefen()
    assert rufe[-1] == ("turn_on", "switch.dock_wz_ladekontakte") and "dock_wz" not in ll.aus

    # Direkt danach wird nicht erneut gesperrt (10 min Nachladen) ...
    meldung(81)
    await ll.pruefen()
    assert rufe[-1][0] == "turn_on"
    ll.frei_bis.clear()   # ... 10 min spaeter schon
    # Wieder voll, dann Remote vom Dock genommen (meldet sich 20 min nicht mehr): Kontakte frei fuer die naechste Remote
    meldung(81)
    await ll.pruefen()
    assert rufe[-1][0] == "turn_off"
    meldung(81, alter=20 * 60)
    await ll.pruefen()
    assert rufe[-1][0] == "turn_on"
    ll.frei_bis.clear()

    # 2-Stunden-Grenze: danach wird erst einmal geladen, nicht sofort wieder gesperrt
    meldung(85)
    await ll.pruefen()
    assert rufe[-1][0] == "turn_off"
    ll.aus["dock_wz"]["seit"] -= 3 * 3600
    await ll.pruefen()
    assert rufe[-1][0] == "turn_on"


async def test_ladelimit_ohne_schalter_und_ws(hass, eingerichtet, hass_ws_client):
    ll = hass.data[DOMAIN + "_ladelimit"]
    dock = _Dock()
    hass.data[DOMAIN + "_dockstatus"] = dock
    rufe = _schalter(hass)
    dock.stand["remote-wz-test"] = {"werte": {"a": "95", "l": "1"}, "zeit": time.time(), "dock": "dock_ohne"}

    ws = await hass_ws_client(hass)
    await ws.send_json({"id": 1, "type": "esphomeremote/ladelimit_set", "device": "remote-wz-test", "prozent": 80})
    r = await ws.receive_json()
    assert r["success"] and r["result"]["limits"] == {"remote-wz-test": 80}
    assert rufe == [], "Dock ohne Ladekontakt-Schalter: nichts geschaltet"
    assert ll.kd.data["ladelimit"] == {"remote-wz-test": 80}

    await ws.send_json({"id": 2, "type": "esphomeremote/ladelimit_set", "device": "remote-wz-test", "prozent": 30})
    assert not (await ws.receive_json())["success"], "unter 50 % nicht erlaubt"
    await ws.send_json({"id": 3, "type": "esphomeremote/ladelimit_set", "device": "remote-wz-test", "prozent": 0})
    r = await ws.receive_json()
    assert r["success"] and r["result"]["limits"] == {}
