"""Entitaeten der Remotes/Docks mit Bereich/Anzeigenamen vor der entity_id finden (2026-11-28)."""
from __future__ import annotations


async def test_umbenannte_entitaeten(hass):
    from homeassistant.helpers import entity_registry as er
    from pytest_homeassistant_custom_component.common import MockConfigEntry

    from custom_components.esphomeremote.dock_entitaeten import aufloesen, entitaeten_karte, geraet_ids, ueblich
    from custom_components.esphomeremote.zustaende import remote_wert

    ce = MockConfigEntry(domain="esphome", data={"device_name": "remote-wz-test"}, unique_id="AA:BB:CC:DD:EE:01")
    ce.add_to_hass(hass)
    reg = er.async_get(hass)
    reg.async_get_or_create("sensor", "esphome", "akku", config_entry=ce, original_name="Akkustand",
                            suggested_object_id="wohnzimmer_remote_wz_test_akkustand")
    reg.async_get_or_create("sensor", "esphome", "akt", config_entry=ce, original_name="Aktivität",
                            suggested_object_id="remote_wz_test_aktivitat")
    echt = "sensor.wohnzimmer_remote_wz_test_akkustand"
    assert aufloesen(hass, "sensor.remote_wz_test_akkustand") == echt
    assert ueblich(hass, echt) == "sensor.remote_wz_test_akkustand"
    assert aufloesen(hass, "sensor.remote_wz_test_aktivitat") == "sensor.remote_wz_test_aktivitat"   # passt schon
    assert entitaeten_karte(hass) == {"sensor.remote_wz_test_akkustand": echt}
    assert echt in geraet_ids(hass, "remote-wz-test")

    # remote_wert liest die echte Entitaet und faellt auf den Dock-Wert der UEBLICHEN ID zurueck
    hass.states.async_set(echt, "61")
    assert remote_wert(hass, "sensor.remote_wz_test_akkustand").state == "61"
    hass.states.async_set(echt, "unavailable")
    hass.states.async_set("sensor.esphomeremote_remote_wz_test_akkustand", "58")
    assert remote_wert(hass, "sensor.remote_wz_test_akkustand").state == "58"

    # Registry-Aenderung baut die Karte neu
    reg.async_update_entity(echt, new_entity_id="sensor.remote_wz_test_akkustand")
    await hass.async_block_till_done()
    assert aufloesen(hass, "sensor.remote_wz_test_akkustand") == "sensor.remote_wz_test_akkustand"


async def test_karte_nur_eigene_geraete(hass):
    from homeassistant.helpers import entity_registry as er
    from pytest_homeassistant_custom_component.common import MockConfigEntry

    from custom_components.esphomeremote.dock_entitaeten import entitaeten_karte
    reg = er.async_get(hass)
    for name, mac in (("dock-wz-test", "AA:BB:CC:DD:EE:02"), ("waage", "AA:BB:CC:DD:EE:03")):
        ce = MockConfigEntry(domain="esphome", data={"device_name": name}, unique_id=mac)
        ce.add_to_hass(hass)
        reg.async_get_or_create("sensor", "esphome", f"{name}-x", config_entry=ce, original_name="Laufzeit",
                                suggested_object_id=f"wohnzimmer_{name.replace('-', '_')}_laufzeit")
    assert entitaeten_karte(hass, ["dock_wz_test"]) == {"sensor.dock_wz_test_laufzeit": "sensor.wohnzimmer_dock_wz_test_laufzeit"}
    assert len(entitaeten_karte(hass)) == 2
