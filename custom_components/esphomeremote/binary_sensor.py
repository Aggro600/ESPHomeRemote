"""Binaer-Sensoren der Integration (zustaende.py): Kamera auf dem TV, Werte der Remote ueber das Dock."""

from __future__ import annotations

DOMAIN = "esphomeremote"


async def async_setup_platform(hass, config, add, discovery_info=None) -> None:
    if discovery_info is not None and discovery_info.get("zustand"):
        hass.data[DOMAIN + "_zustaende"].plattform("binary_sensor", add)
