"""Diagnose-Datei: vollstaendig und ohne Persoenliches."""
from __future__ import annotations

import json


async def test_diagnose_anonym(hass, eingerichtet):
    from custom_components.esphomeremote.diagnostics import async_get_config_entry_diagnostics

    kd = hass.data["esphomeremote"]
    # Etwas Persoenliches in den Stand legen, das NICHT in der Datei landen darf
    kd.entry("remote-wz-test")["config"]["activities"][0]["name"] = "Fernseher von Familie Mustermann"
    hass.states.async_set("sensor.remote_wz_test_akku", "73")
    hass.states.async_set("binary_sensor.remote_wz_test_wlan_aktiv", "on")

    d = await async_get_config_entry_diagnostics(hass, eingerichtet)
    text = json.dumps(d, ensure_ascii=False)

    assert "remote-wz-test" not in text and "remote_wz_test" not in text, "Geraetename steht in der Diagnose"
    assert "Mustermann" not in text, "Inhalte der Belegung stehen in der Diagnose"
    assert "testschluessel" not in text, "Schluessel steht in der Diagnose"

    assert d["umgebung"]["supervisor"] is False
    assert d["umgebung"]["integration"]
    assert set(d["remotes"]) == {"remote_1", "remote_2"}
    r1 = d["remotes"]["remote_1"]
    assert r1["akku"] == "73" and r1["wlan_aktiv"] == "on"
    assert r1["belegung"]["aktivitaeten"] >= 2 and r1["belegung"]["bytes"] > 100
    assert "fehler" in d["firmware"] or "addon_fehler" in d["firmware"]   # kein Add-on: gemeldet, nicht abgestuerzt
    assert isinstance(d["reparaturen"], list)


async def test_diagnose_tilgt_adressen():
    from custom_components.esphomeremote.diagnostics import _Anonym

    a = _Anonym(["remote-wz-kueche"])
    s = a.text("remote-wz-kueche (remote_wz_kueche) unter 192.168.1.23, BLE AA:BB:CC:DD:EE:01")
    assert s == "remote_1 (remote_1) unter <ip>, BLE <mac>"
    assert a.name("dock-wz", "dock") == "dock_1" and a.name("dock-wz", "dock") == "dock_1"
