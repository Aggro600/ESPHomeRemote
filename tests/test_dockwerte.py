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


async def test_ereignis_ueber_das_dock(hass, eingerichtet):
    """Remote ohne eigene HA-Verbindung: das Dock ruft remote_event auf, HA feuert das Ereignis (nur erlaubte)."""
    from homeassistant.core import callback
    gesehen = []

    @callback
    def merken(ev):
        gesehen.append((ev.event_type, dict(ev.data)))

    hass.bus.async_listen("esphome.remote_live_start", merken)
    hass.bus.async_listen("esphome.remote_bt_status", merken)
    await hass.services.async_call(DOMAIN, "remote_event",
                                   {"geraet": "remote-wz-test", "event": "esphome.remote_live_start",
                                    "kamera": "camera.tuer", "d": "dock_test"}, blocking=True)
    await hass.services.async_call(DOMAIN, "remote_event",
                                   {"geraet": "remote-wz-test", "event": "esphome.remote_bt_status"}, blocking=True)
    await hass.async_block_till_done()
    assert gesehen == [("esphome.remote_live_start", {"geraet": "remote-wz-test", "kamera": "camera.tuer"})]


async def test_helligkeit_im_dock_stand(hass, eingerichtet):
    """Helligkeit nach Lichtsensor fuer Remotes ohne eigene HA-Verbindung: "hb" im Dock-Stand, erst ab 3 Punkten neu,
    und das Uebernehmen auf der Remote zaehlt nicht als Verstellen von Hand."""
    hass.states.async_set("sensor.licht_test", "50", {"device_class": "illuminance"})
    licht = hass.data["esphomeremote_licht"]
    await licht.setzen({"sensor": "sensor.licht_test"})
    disp, kbd = licht.dock_werte("remote-wz-test")
    assert (disp, kbd) == (75, 60)
    hass.states.async_set("sensor.licht_test", "60")   # Kurve: 77 / 62 - unter der Schwelle
    assert licht.dock_werte("remote-wz-test") == [75, 60]
    hass.states.async_set("sensor.licht_test", "400")
    assert licht.dock_werte("remote-wz-test") == [100, 80]
    punkte = [list(p) for p in licht.cfg["ziele"]["disp"]["punkte"]]
    licht.einst_gemeldet("remote-wz-test", {"disp_bri": 75}, {"disp_bri": 100})   # Echo vom Dock-Stand
    assert licht.cfg["ziele"]["disp"]["punkte"] == punkte
    licht.cfg["aus"] = ["remote-wz-test"]
    assert licht.dock_werte("remote-wz-test") == [-1, -1]
