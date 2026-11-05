"""Transition after the rename from esphomeremote_konfig to esphomeremote (2026-11-02).

Remotes and docks with older firmware still call the old service names, use the old /api paths (extra_urls on the
views) and read sensor.esphomeremote_konfig_<remote>. Remove this module once all devices run the new firmware.
"""
from __future__ import annotations

from homeassistant.core import HomeAssistant, ServiceCall
from homeassistant.helpers.service import async_set_service_schema

DOMAIN = "esphomeremote"
OLD_DOMAIN = "esphomeremote_konfig"
SERVICES = {
    "an_remotes": "to_remotes", "blaster_senden": "blaster_send", "einst_melden": "settings_report",
    "kamera_groesse": "camera_size", "kamera_position": "camera_position", "kamera_tv": "camera_tv", "licht": "light",
    "neustart": "restart", "regeln": "rules", "remote_log": "remote_log", "remote_status": "remote_status",
    "timer_neu_starten": "timer_restart", "timer_verlaengern": "timer_extend", "wlan_kommt": "wifi_coming",
}


def old_sensor(slug: str) -> str:
    return f"sensor.{OLD_DOMAIN}_{slug}"


def setup(hass: HomeAssistant) -> None:
    """Forward the old service names to the new ones."""
    for old, new in SERVICES.items():
        async def forward(call: ServiceCall, new: str = new) -> None:
            await hass.services.async_call(DOMAIN, new, dict(call.data), blocking=True, context=call.context)
        hass.services.async_register(OLD_DOMAIN, old, forward)
        # Beschreibung gleich mitgeben (2026-11-05): sonst sucht HA bei jedem Start eine services.yaml der
        # Integration "esphomeremote_konfig" und meldet "Failed to load services.yaml" (es gibt sie nicht mehr)
        async_set_service_schema(hass, OLD_DOMAIN, old, {
            "name": f"{old} (alt)",
            "description": f"Alter Name aus der Zeit vor der Umbenennung - leitet an {DOMAIN}.{new} weiter (alte Firmware).",
            "fields": {}})
