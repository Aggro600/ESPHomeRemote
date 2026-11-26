"""Entitaeten der Docks sicher finden (2026-11-26)."""
from __future__ import annotations

from homeassistant.core import HomeAssistant


def dock_entitaet(hass: HomeAssistant, dock: str, name: str, domain: str = "sensor") -> str:
    """Entitaet eines Docks ueber das Geraet (ESPHome-Name) und den Namen aus der Firmware (original_name) - nicht ueber
    die entity_id: HA setzt teils Bereich/Anzeigenamen davor (2026-11-26: sensor.wohnzimmer_dock_wz_bluetooth_geraete;
    bei einem Tester fehlten dadurch die Bluetooth-Plaetze im Konfigurator). Ohne Treffer: die uebliche entity_id."""
    from homeassistant.helpers import entity_registry as er
    reg = er.async_get(hass)
    for ce in hass.config_entries.async_entries("esphome"):
        if str(ce.data.get("device_name") or "").replace("-", "_") != dock:
            continue
        for e in er.async_entries_for_config_entry(reg, ce.entry_id):
            if e.domain == domain and (e.original_name or "") == name:
                return e.entity_id
    slug = name.lower().replace("ä", "ae").replace("ö", "oe").replace("ü", "ue").replace("-", "_").replace(" ", "_")
    return f"{domain}.{dock}_{slug}"


def dock_entitaeten(hass: HomeAssistant, docks) -> dict:
    """{dock: {"bt": .., "status": ..}} fuer den Konfigurator."""
    return {d: {"bt": dock_entitaet(hass, d, "Bluetooth-Geraete"), "status": dock_entitaet(hass, d, "Status", "binary_sensor")}
            for d in docks}
