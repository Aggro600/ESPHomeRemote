"""Werte der Remote ueber das Dock: Akkustand und -spannung mit Langzeitstatistik (state_class measurement)."""
from __future__ import annotations

DOMAIN = "esphomeremote"


async def test_akku_ueber_dock_mit_statistik(hass, eingerichtet):
    await hass.services.async_call(DOMAIN, "remote_status",
                                   {"geraet": "remote-wz-test", "a": "56", "v": "3.92", "k": "1", "l": "1",
                                    "d": "dock_test"}, blocking=True)
    await hass.async_block_till_done()
    akku = hass.states.get("sensor.esphomeremote_remote_wz_test_akkustand")
    spannung = hass.states.get("sensor.esphomeremote_remote_wz_test_akkuspannung")
    assert akku.state == "56.0" and akku.attributes["state_class"] == "measurement"
    assert akku.attributes["unit_of_measurement"] == "%" and akku.attributes["quelle"] == "dock"
    assert spannung.attributes["state_class"] == "measurement"
    # Werte ohne Einheit bleiben ohne Statistik
    assert "state_class" not in hass.states.get("sensor.esphomeremote_remote_wz_test_aktivitat").attributes
    assert hass.states.get("binary_sensor.esphomeremote_remote_wz_test_ladekabel").state == "on"
